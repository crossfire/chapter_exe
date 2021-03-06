// 入力クラス

#pragma once

#include "stdafx.h"

#include <memory>
#include <vector>
#include <algorithm>
#include <iostream>

// 環境依存部分の定義
#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
typedef uint64_t __int64;
template<typename T, typename S> inline T min(T a, S b) { return (b > a) ? a : b; }
template<typename T> static void ZeroMemory(T* dst, size_t len){ memset(dst, 0, len); }
constexpr int WAVE_FORMAT_PCM = 0x0001; // Microsoft PCM format
#endif

class Source {
  public:
    virtual void init(char *infile) = 0;

    virtual bool has_video() = 0;
    virtual bool has_audio() = 0;
    virtual INPUT_INFO &get_input_info() = 0;
    virtual void set_rate(int rate, int scale) = 0;

    virtual bool read_video_y8(int frame, unsigned char *luma) = 0;
    virtual int read_audio(int frame, short *buf) = 0;
};

// 空のソース
class NullSource : public Source {
  protected:
    NullSource() : _ip() { }
    virtual ~NullSource() { }

    INPUT_INFO _ip;
  public:

    bool has_video() { return (_ip.flag & INPUT_INFO_FLAG_VIDEO) != 0; };
    bool has_audio() { return (_ip.flag & INPUT_INFO_FLAG_AUDIO) != 0; };
    INPUT_INFO &get_input_info() { return _ip; };
    void set_rate(int rate, int scale) {
      _ip.rate = rate;
      _ip.scale = scale;
    }

    // must implement
    void init(char *infile) { };
    bool read_video_y8(int frame, unsigned char *luma) { return false; };
    int read_audio(int frame, short *buf) { return 0; };
};

#ifdef _WIN32
// auiを使ったソース
class AuiSource : public NullSource {
  protected:
    string _in, _plugin;

    HMODULE _dll;

    INPUT_PLUGIN_TABLE *_ipt;
    INPUT_HANDLE _ih;
    //INPUT_INFO _ip;

  public:
    AuiSource(void) : NullSource(), _dll(NULL) { }
    virtual ~AuiSource() {
      if (_dll) {
        FreeLibrary(_dll);
      }
    }

    virtual void init(char *infile) {
      _in = infile;
      _plugin = "avsinp.aui";

      int p = (int)_in.find("://");
      if (p != _in.npos) {
        _plugin = _in.substr(0, p);
        _in = _in.substr(p+3);
      }

      //printf(" -%s\n", _plugin.c_str());

      _dll = LoadLibrary(_plugin.c_str());
      if (_dll == NULL) {
        throw "   plugin loading failed.";
      }

      FARPROC f = GetProcAddress(_dll, _T("GetInputPluginTable"));
      if (f == NULL) {
        throw "   not Aviutl input plugin error.";
      }
      _ipt = (INPUT_PLUGIN_TABLE*)f();
      if (_ipt == NULL) {
        throw "   not Aviutl input plugin error.";
      }
      if (_ipt->func_init) {
        if (_ipt->func_init() == FALSE) {
          throw "   func_init() failed.";
        }
      }

      _ih = _ipt->func_open((LPSTR)_in.c_str());
      if (_ih == NULL) {
        throw "   func_open() failed.";
      }

      if (_ipt->func_info_get(_ih, &_ip) == FALSE) {
        throw "   func_info_get() failed...";
      }
    }

    bool has_video() {
      return (_ip.flag & INPUT_INFO_FLAG_VIDEO) != 0;
    }
    bool has_audio() {
      return (_ip.flag & INPUT_INFO_FLAG_AUDIO) != 0;
    }

    INPUT_INFO &get_input_info() {
      return _ip;
    }

    bool read_video_y8(int frame, unsigned char *luma) {
      int h = _ip.format->biHeight;
      int w = _ip.format->biWidth;
      unsigned char *buf = (unsigned char *)malloc(2 * h * w);

      int ret = _ipt->func_read_video(_ih, frame, buf);
      if (ret == 0) {
        return false;
      }

      int skip_w = w & 0x0F;
      w = w - skip_w;

      unsigned char *p = buf;
      for (int i=0; i<w; i++) {
        for (int j=0; j<h; j++) {
          *luma = *p;

          luma++;
          p += 2;
        }
        p += skip_w * 2;
      }
      free(buf);
      return true;
    }

    int read_audio(int frame, short *buf) {
      int start = (int)((double)frame * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
      int end = (int)((double)(frame + 1) * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
      return _ipt->func_read_audio(_ih, start, end - start, buf);
    }
};
#endif

// *.wavソース
class WavSource : public NullSource {
  string _in;

  FILE *_f;
  __int64 _start;
  __int64 _end;
  WAVEFORMATEX _fmt;

  public:
  WavSource() : NullSource(), _f(NULL), _start(0) { }
  ~WavSource() {
    if (_f) {
      fclose(_f);
    }
  }

  void init(char *infile) {
    printf(" -WavSource\n");
    _f = fopen(infile, "rb");
    if (_f == NULL) {
      throw "   wav open failed.";
    }

    char buf[1000];
    if (fread(buf, 1, 4, _f) != 4 || strncmp(buf, "RIFF", 4) != 0) {
      throw "   no RIFF header.";
    }
    fseek(_f, 4, SEEK_CUR);
    if (fread(buf, 1, 4, _f) != 4 || strncmp(buf, "WAVE", 4) != 0) {
      throw "   no WAVE header.";
    }

    // chunk
    while(fread(buf, 1, 4, _f) == 4) {
      if (ftell(_f) > 1000000) {
        break;				
      }

      int size = 0;
      fread(&size, 4, 1, _f);
      if (strncmp(buf, "fmt ", 4) == 0) {
        if (fread(&_fmt, min(size, sizeof(_fmt)), 1, _f) != 1) {
          throw "   illegal WAVE file.";
        }
        if (_fmt.wFormatTag != WAVE_FORMAT_PCM) {
          throw "   only PCM supported.";
        }
        int diff = size - sizeof(_fmt);
        if (diff > 0) {
          fseek(_f, size - sizeof(_fmt), SEEK_CUR);
        }
      } else if (strncmp(buf, "data", 4) == 0){
        fseek(_f, 4, SEEK_CUR);
#ifdef _WIN32
        _start = _ftelli64(_f);
#else
        _start = ftello(_f);
#endif
        break;
      } else {
        fseek(_f, size, SEEK_CUR);
      }
    }
    if (_start == 0) {
      fclose(_f);
      throw "   maybe not wav file.";
    }

    ZeroMemory(&_ip, sizeof(_ip));
    _ip.flag |= INPUT_INFO_FLAG_AUDIO;
    _ip.audio_format = &_fmt;
    _ip.audio_format_size = sizeof(_fmt);
    _ip.audio_n = -1;
  }

  int read_audio(int frame, short *buf) {
    __int64 start = (int)((double)frame * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
    __int64 end = (int)((double)(frame + 1) * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);

#ifdef _WIN32
    _fseeki64(_f, _start + start * _fmt.nBlockAlign, SEEK_SET);
#else
    fseeko(_f, _start + start * _fmt.nBlockAlign, SEEK_SET);
#endif

    return (int)fread(buf, _fmt.nBlockAlign, (size_t)(end - start), _f);
  }
};

#ifdef _WIN32
// avisynthにリンクしているので
#define AVS_LINKAGE_DLLIMPORT
#include "avisynth.h"
#pragma comment(lib, "avisynth.lib")

static void DeleteScriptEnvironment(IScriptEnvironment2* env) {
  if (env) env->DeleteScriptEnvironment();
}

typedef std::unique_ptr<IScriptEnvironment2, decltype(&DeleteScriptEnvironment)> ScriptEnvironmentPointer;

static ScriptEnvironmentPointer make_unique_ptr(IScriptEnvironment2* env) {
  return ScriptEnvironmentPointer(env, DeleteScriptEnvironment);
}

// AviSynthクリップのソース
class AvsSource : public NullSource {
  protected:
    ScriptEnvironmentPointer env_;
    PClip clip;

    BITMAPINFOHEADER format;
    WAVEFORMATEX audio_format;

  public:
    AvsSource(void) 
      : NullSource()
        , env_(make_unique_ptr(CreateScriptEnvironment2()))
        , format()
        , audio_format()
  { }

    virtual void init(char *infile) {
      try {
        clip = env_->Invoke("Import", infile, 0).AsClip();

        VideoInfo vi = clip->GetVideoInfo();

        // サポートしていないフォーマットは変換
        if (vi.BitsPerComponent() != 8 || vi.IsPlanar() == false) {
          clip = env_->Invoke("ConvertToY8", clip).AsClip();
          vi = clip->GetVideoInfo();
        }
        if (vi.num_audio_samples > 0 && vi.sample_type != SAMPLE_INT16) {
          clip = env_->Invoke("ConvertAudioTo16bit", clip).AsClip();
          vi = clip->GetVideoInfo();
        }

        // 面倒なので使ってるメンバだけ
        _ip.flag = INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS | INPUT_INFO_FLAG_VIDEO;
        if (vi.num_audio_samples > 0) {
          _ip.flag |= INPUT_INFO_FLAG_AUDIO;
        }
        _ip.rate = vi.fps_numerator;
        _ip.scale = vi.fps_denominator;
        _ip.n = vi.num_frames;
        _ip.format = &format;
        format.biHeight = vi.height;
        format.biWidth = vi.width;
        // 48kHzで12時間を超えるとINT_MAXを越えてしまうが表示にしか使っていないのでOK
        _ip.audio_n = (int)std::min<int64_t>(INT_MAX, vi.num_audio_samples);
        _ip.audio_format = &audio_format;
        audio_format.nChannels = vi.AudioChannels();
        audio_format.nSamplesPerSec = vi.audio_samples_per_second;
      }
      catch (const AvisynthError& err) {
        printf("%s\n", err.msg);

        char path[MAX_PATH];
        HMODULE hm = NULL;

        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
              (LPCSTR)&CreateScriptEnvironment2,
              &hm))
        {
          GetModuleFileNameA(hm, path, sizeof(path));
          printf("Check the Avisynth: %s\n", path);
          printf("And the script file: %s\n", infile);
        }

        throw "AvisynthError";
      }
    }

    bool has_video() {
      return (_ip.flag & INPUT_INFO_FLAG_VIDEO) != 0;
    }
    bool has_audio() {
      return (_ip.flag & INPUT_INFO_FLAG_AUDIO) != 0;
    }

    INPUT_INFO &get_input_info() {
      return _ip;
    }

    bool read_video_y8(int frame, unsigned char *luma) {
      PVideoFrame pframe = clip->GetFrame(frame, env_.get());
      const unsigned char* src = pframe->GetReadPtr(PLANAR_Y);
      int pitch = pframe->GetPitch(PLANAR_Y);

      int w = _ip.format->biWidth & 0xFFFFFFF0;
      int h = _ip.format->biHeight & 0xFFFFFFF0;
      env_->BitBlt(luma, w, src, pitch, w, h);

      return true;
    }

    int read_audio(int frame, short *buf) {
      int64_t start = (int64_t)((double)frame * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
      int64_t end = (int64_t)((double)(frame + 1) * _ip.audio_format->nSamplesPerSec / _ip.rate * _ip.scale);
      clip->GetAudio(buf, start, end - start, env_.get());
      return int(end - start);
    }
};
#endif

// VapourSynth
#include <VapourSynth.h>
#include <VSScript.h>
#include <VSHelper.h>

class VsSource : public NullSource {
  protected:
    VSCore* core;
    VSScript* handle;
    const VSAPI* env_;
    VSNodeRef* clip;

    BITMAPINFOHEADER format;
    WAVEFORMATEX audio_format;

  public:
    VsSource(void) 
      : NullSource()
        , format()
        , audio_format()
  {
    vsscript_init();
    this->env_ = vsscript_getVSApi2(VAPOURSYNTH_API_VERSION);
  }
    ~VsSource(){
      vsscript_finalize();
    }

    virtual void init(char *infile) {
      try {
        if (vsscript_evaluateFile(&this->handle, infile, efSetWorkingDir) != 0)
          throw "failed to open file";
        this->core = vsscript_getCore(this->handle);
        this->clip = vsscript_getOutput(this->handle, 0);

        const VSVideoInfo* vi = this->env_->getVideoInfo(this->clip);

        // pfGray8 以外は変換
        if (vi->format->bitsPerSample != 8) throw "8-bit clip only allowed!";
        if (vi->format->id != pfGray8){
          auto stdp = this->env_->getPluginById("com.vapoursynth.resize", this->core);
          auto args = this->env_->createMap();
          this->env_->propSetNode(args, "clip", this->clip, paReplace);
          this->env_->propSetInt(args, "format", pfGray8, paReplace);
          auto ret = this->env_->invoke(stdp, "Bilinear", args);
          this->env_->freeMap(args);
          if (const char* err = this->env_->getError(ret)) throw err;
          this->env_->freeNode(this->clip);
          this->clip = this->env_->propGetNode(ret, "clip", 0, nullptr);
          this->env_->freeMap(ret);
        }

        // 面倒なので使ってるメンバだけ
        _ip.flag = INPUT_INFO_FLAG_VIDEO_RANDOM_ACCESS | INPUT_INFO_FLAG_VIDEO;
        _ip.rate = vi->fpsNum;
        _ip.scale = vi->fpsDen;
        _ip.n = vi->numFrames;
        _ip.format = &this->format;
        format.biHeight = vi->height;
        format.biWidth = vi->width;
      }
      catch (const char* str) {
        std::cerr << str << std::endl;
      }
    }

    bool has_video() {
      return (_ip.flag & INPUT_INFO_FLAG_VIDEO) != 0;
    }
    bool has_audio() {
      return false;
    }

    INPUT_INFO &get_input_info() {
      return _ip;
    }

    bool read_video_y8(int frame, unsigned char *luma) {
      const VSFrameRef* pframe = this->env_->getFrame(frame, this->clip, nullptr, 0);
      auto src = this->env_->getReadPtr(pframe, 0);
      int stride = this->env_->getStride(pframe, 0);

      int w = _ip.format->biWidth & 0xFFFFFFF0;
      int h = _ip.format->biHeight & 0xFFFFFFF0;
      vs_bitblt(luma, w, src, stride, w, h);

      return true;
    }

    int read_audio(int frame, short* buf){
      return -1;
    }
};
