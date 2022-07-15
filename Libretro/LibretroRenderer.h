#pragma once
#include "../Core/IRenderingDevice.h"
#include "../Core/VideoRenderer.h"
#include "../Core/EmuSettings.h"
#include "../Core/Console.h"
#include "../Utilities/snes_ntsc.h"
#include "libretro.h"

class LibretroRenderer : public IRenderingDevice
{
private:
	shared_ptr<Console> _console;
	retro_video_refresh_t _sendFrame = nullptr;
	retro_environment_t _retroEnv = nullptr;
	bool _skipMode = false;
	int32_t _previousHeight = -1;
	int32_t _previousWidth = -1;

public:
	LibretroRenderer(shared_ptr<Console> console, retro_environment_t retroEnv)
	{
		_console = console;
		_retroEnv = retroEnv;
		_console->GetVideoRenderer()->RegisterRenderingDevice(this);
	}

	~LibretroRenderer()
	{
		_console->GetVideoRenderer()->UnregisterRenderingDevice(this);
	}

	// Inherited via IRenderingDevice
	virtual void UpdateFrame(void *frameBuffer, uint32_t width, uint32_t height) override
	{
		if(!_skipMode && _sendFrame) {
			//Use Blargg's NTSC filter's max size as a minimum resolution, to prevent changing resolution too often
			int32_t newWidth = std::max<int32_t>(width, SNES_NTSC_OUT_WIDTH(256));
			int32_t newHeight = std::max<int32_t>(height, 239 * 2);
			if(_retroEnv != nullptr && (_previousWidth != newWidth || _previousHeight != newHeight)) {
				//Resolution change is needed
				retro_system_av_info avInfo = {};
				GetSystemAudioVideoInfo(avInfo, newWidth, newHeight);
				_retroEnv(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avInfo);
				
				_previousWidth = newWidth;
				_previousHeight = newHeight;
			}

			_sendFrame(frameBuffer, width, height, sizeof(uint32_t) * width);
		}
	}
	
	void GetSystemAudioVideoInfo(retro_system_av_info &info, int32_t maxWidth = 0, int32_t maxHeight = 0)
	{
		AudioConfig audio = _console->GetSettings()->GetAudioConfig();
		
		info.timing.fps = _console->GetRegion() == ConsoleRegion::Ntsc ? 60.098811862348404716732985230828 : 50.006977968268290848936010226333;
		info.timing.sample_rate = audio.SampleRate;

		OverscanDimensions overscan = _console->GetSettings()->GetOverscan();
		int width = (256 - overscan.Left - overscan.Right);
		int height = (239 - overscan.Top - overscan.Bottom);

		double aspectRatio = _console->GetSettings()->GetAspectRatio(_console->GetRegion());
		if(aspectRatio != 0.0) {
			VideoAspectRatio aspect = _console->GetSettings()->GetVideoConfig().AspectRatio;
			bool usePar = aspect == VideoAspectRatio::NTSC || aspect == VideoAspectRatio::PAL || aspect == VideoAspectRatio::Auto;
			if(usePar) {
				info.geometry.aspect_ratio = (float)(width * aspectRatio / height);
			} else {
				info.geometry.aspect_ratio = (float)aspectRatio;
			}
		} else {
			info.geometry.aspect_ratio = (float)width / height;
		}

		info.geometry.base_width = width;
		info.geometry.base_height = height;

		info.geometry.max_width = maxWidth;
		info.geometry.max_height = maxHeight;

		if(maxHeight > 0 && maxWidth > 0) {
			_previousWidth = maxWidth;
			_previousHeight = maxHeight;
		}
	}

	void SetVideoCallback(retro_video_refresh_t sendFrame)
	{
		_sendFrame = sendFrame;
	}

	void SetSkipMode(bool skip)
	{
		_skipMode = skip;
	}
	
	virtual void Render() override
	{
	}

	virtual void Reset() override
	{
	}
};
