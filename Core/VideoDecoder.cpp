#include "stdafx.h"
#include "IRenderingDevice.h"
#include "VideoDecoder.h"
#include "VideoRenderer.h"
#include "DefaultVideoFilter.h"
#include "NotificationManager.h"
#include "Console.h"
#include "RewindManager.h"
#include "EmuSettings.h"
#include "SettingTypes.h"
#include "NtscFilter.h"
#include "ScaleFilter.h"
#include "Ppu.h"
#include "DebugHud.h"
#include "InputHud.h"

VideoDecoder::VideoDecoder(shared_ptr<Console> console)
{
	_console = console;
	_frameChanged = false;
	_stopFlag = false;
	_baseFrameInfo = { 512, 478 };
	_lastFrameInfo = _baseFrameInfo;
	UpdateVideoFilter();
	_videoFilter->SetBaseFrameInfo(_baseFrameInfo);
	_inputHud.reset(new InputHud(console.get()));
}

VideoDecoder::~VideoDecoder()
{
	StopThread();
}

FrameInfo VideoDecoder::GetFrameInfo()
{
	return _lastFrameInfo;
}

ScreenSize VideoDecoder::GetScreenSize(bool ignoreScale)
{
	ScreenSize size;
	FrameInfo frameInfo = _videoFilter->GetFrameInfo();

	double scale = (ignoreScale ? 1 : _console->GetSettings()->GetVideoConfig().VideoScale);
	bool useHighResOutput = _baseFrameInfo.Width >= 512 || _videoFilterType == VideoFilterType::NTSC;
	int divider = useHighResOutput ? 2 : 1;
	size.Width = (int32_t)(frameInfo.Width * scale / divider);
	size.Height = (int32_t)(frameInfo.Height * scale / divider);
	size.Scale = scale;

	double aspectRatio = _console->GetSettings()->GetAspectRatio(_console->GetRegion());
	if(aspectRatio != 0.0) {
		VideoAspectRatio aspect = _console->GetSettings()->GetVideoConfig().AspectRatio;
		bool usePar = aspect == VideoAspectRatio::NTSC || aspect == VideoAspectRatio::PAL || aspect == VideoAspectRatio::Auto;
		if(usePar) {
			OverscanDimensions overscan = _console->GetSettings()->GetOverscan();
			uint32_t fullWidth = frameInfo.Width + (overscan.Left + overscan.Right);
			size.Width = (uint32_t)(256 * scale * aspectRatio * frameInfo.Width / fullWidth);
		} else {
			size.Width = (uint32_t)(size.Height * aspectRatio);
		}
	}

	return size;
}

void VideoDecoder::UpdateVideoFilter()
{
	VideoFilterType newFilter = _console->GetSettings()->GetVideoConfig().VideoFilter;

	if(_videoFilterType != newFilter || _videoFilter == nullptr) {
		_videoFilterType = newFilter;
		_videoFilter.reset(new DefaultVideoFilter(_console));
		_scaleFilter.reset();

		switch(_videoFilterType) {
			case VideoFilterType::None: break;
			case VideoFilterType::NTSC: _videoFilter.reset(new NtscFilter(_console)); break;
			default: _scaleFilter = ScaleFilter::GetScaleFilter(_videoFilterType); break;
		}
	}
}

void VideoDecoder::DecodeFrame(bool forRewind)
{
	UpdateVideoFilter();

	_videoFilter->SetBaseFrameInfo(_baseFrameInfo);
	_videoFilter->SendFrame(_ppuOutputBuffer, _frameNumber);

	uint32_t* outputBuffer = _videoFilter->GetOutputBuffer();
	FrameInfo frameInfo = _videoFilter->GetFrameInfo();
	
	_inputHud->DrawControllers(_videoFilter->GetOverscan(), _frameNumber);
	_console->GetDebugHud()->Draw(outputBuffer, _videoFilter->GetOverscan(), frameInfo.Width, _frameNumber);

	if(_scaleFilter) {
		outputBuffer = _scaleFilter->ApplyFilter(outputBuffer, frameInfo.Width, frameInfo.Height, _console->GetSettings()->GetVideoConfig().ScanlineIntensity);
		frameInfo = _scaleFilter->GetFrameInfo(frameInfo);
	}

	ScreenSize screenSize = GetScreenSize(true);
	VideoConfig config = _console->GetSettings()->GetVideoConfig();
	if(_previousScale != config.VideoScale || screenSize.Height != _previousScreenSize.Height || screenSize.Width != _previousScreenSize.Width) {
		_console->GetNotificationManager()->SendNotification(ConsoleNotificationType::ResolutionChanged);
	}
	_previousScale = config.VideoScale;
	_previousScreenSize = screenSize;
	_lastFrameInfo = frameInfo;

	//Rewind manager will take care of sending the correct frame to the video renderer
	_console->GetRewindManager()->SendFrame(outputBuffer, frameInfo.Width, frameInfo.Height, forRewind);

	_frameChanged = false;
}

void VideoDecoder::DecodeThread()
{
	//This thread will decode the PPU's output (color ID to RGB, intensify r/g/b and produce a HD version of the frame if needed)
	while(!_stopFlag.load()) {
		//DecodeFrame returns the final ARGB frame we want to display in the emulator window
		while(!_frameChanged) {
			_waitForFrame.Wait();
			if(_stopFlag.load()) {
				return;
			}
		}

		DecodeFrame();
	}
}

uint32_t VideoDecoder::GetFrameCount()
{
	return _frameCount;
}

void VideoDecoder::UpdateFrameSync(uint16_t *ppuOutputBuffer, uint16_t width, uint16_t height, uint32_t frameNumber, bool forRewind)
{
	if(_frameChanged) {
		//Last frame isn't done decoding yet - sometimes Signal() introduces a 25-30ms delay
		while(_frameChanged) {
			//Spin until decode is done
		}
		//At this point, we are sure that the decode thread is no longer busy
	}
	
	_frameChanged = true;
	_baseFrameInfo.Width = width;
	_baseFrameInfo.Height = height;
	_frameNumber = frameNumber;
	_ppuOutputBuffer = ppuOutputBuffer;
	DecodeFrame(forRewind);
	_frameCount++;
}

void VideoDecoder::UpdateFrame(uint16_t *ppuOutputBuffer, uint16_t width, uint16_t height, uint32_t frameNumber)
{
	if(_frameChanged) {
		//Last frame isn't done decoding yet - sometimes Signal() introduces a 25-30ms delay
		while(_frameChanged) {
			//Spin until decode is done
		}
		//At this point, we are sure that the decode thread is no longer busy
	}
	
	_baseFrameInfo.Width = width;
	_baseFrameInfo.Height = height;
	_frameNumber = frameNumber;
	_ppuOutputBuffer = ppuOutputBuffer;
	_frameChanged = true;
	_waitForFrame.Signal();

	_frameCount++;
}

void VideoDecoder::StartThread()
{
#ifndef LIBRETRO
	if(!_decodeThread) {	
		_stopFlag = false;
		_frameChanged = false;
		_frameCount = 0;
		_waitForFrame.Reset();
		
		_decodeThread.reset(new thread(&VideoDecoder::DecodeThread, this));
	}
#endif
}

void VideoDecoder::StopThread()
{
#ifndef LIBRETRO
	_stopFlag = true;
	if(_decodeThread) {
		_waitForFrame.Signal();
		_decodeThread->join();

		_decodeThread.reset();

		//Clear whole screen
		if(_frameCount > 0) {
			vector<uint16_t> outputBuffer(512 * 478, 0);
			_ppuOutputBuffer = outputBuffer.data();
			memset(_ppuOutputBuffer, 0, 512 * 478 * 2);
			DecodeFrame();
			_ppuOutputBuffer = nullptr;
		}
	}
#endif
}

bool VideoDecoder::IsRunning()
{
	return _decodeThread != nullptr;
}

void VideoDecoder::TakeScreenshot()
{
	if(_videoFilter) {
		_videoFilter->TakeScreenshot(_console->GetRomInfo().RomFile.GetFileName(), _videoFilterType);
	}
}

void VideoDecoder::TakeScreenshot(std::stringstream &stream)
{
	if(_videoFilter) {
		_videoFilter->TakeScreenshot(_videoFilterType, "", &stream);
	}
}
