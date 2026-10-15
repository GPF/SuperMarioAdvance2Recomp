// KOS implementation of GBARecomp's existing host-window boundary.
// Kept in SMA2 so the separate GBARecomp checkout remains untouched.
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/sound/stream.h>
#include <dc/video.h>
#include <cstdio>
#include <cstring>

namespace gbarecomp {
namespace {
constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr int kGameWidth = 240;
constexpr int kGameHeight = 160;
constexpr std::size_t kAudioRingSamples = 8192;
constexpr int kAudioStreamBufferBytes = 16 * 1024;

alignas(32) int16_t audio_ring[kAudioRingSamples]{};
alignas(32) int16_t audio_stream_buffer[kAudioStreamBufferBytes / sizeof(int16_t)]{};
uint64_t audio_head = 0;
uint64_t audio_tail = 0;
snd_stream_hnd_t audio_stream = SND_STREAM_INVALID;

void* audio_stream_callback(snd_stream_hnd_t, int requested_bytes,
                            int* received_bytes) {
    if (!received_bytes) return nullptr;
    if (requested_bytes < 0) requested_bytes = 0;
    if (requested_bytes > kAudioStreamBufferBytes)
        requested_bytes = kAudioStreamBufferBytes;
    requested_bytes &= ~3; // KOS PCM16 stream transfers are 4-byte aligned.
    const std::size_t requested_samples =
        static_cast<std::size_t>(requested_bytes) / sizeof(int16_t);
    for (std::size_t i = 0; i < requested_samples; ++i) {
        if (audio_tail != audio_head) {
            audio_stream_buffer[i] = audio_ring[audio_tail & (kAudioRingSamples - 1)];
            ++audio_tail;
        } else {
            audio_stream_buffer[i] = 0;
        }
    }
    *received_bytes = requested_bytes;
    return audio_stream_buffer;
}

bool start_audio_stream() {
    if (audio_stream != SND_STREAM_INVALID) return true;
    audio_head = audio_tail = 0;
    if (snd_stream_init() < 0) {
        std::fprintf(stderr, "[dreamcast-audio] snd_stream_init failed\n");
        return false;
    }
    audio_stream = snd_stream_alloc(audio_stream_callback,
                                    kAudioStreamBufferBytes);
    if (audio_stream == SND_STREAM_INVALID) {
        snd_stream_shutdown();
        std::fprintf(stderr, "[dreamcast-audio] snd_stream_alloc failed\n");
        return false;
    }
    snd_stream_start(audio_stream, 32768, 0); // GBA mono PCM at 32 kHz.
    std::fprintf(stderr, "[dreamcast-audio] KOS mono PCM stream started at 32768 Hz\n");
    return true;
}

void stop_audio_stream() {
    if (audio_stream == SND_STREAM_INVALID) return;
    snd_stream_stop(audio_stream);
    snd_stream_destroy(audio_stream);
    audio_stream = SND_STREAM_INVALID;
    snd_stream_shutdown();
}

uint16_t gba_keyinput() {
    uint16_t keys = 0x03ff;
    maple_device_t* device = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!device) return keys;
    const auto* state = static_cast<const cont_state_t*>(maple_dev_status(device));
    if (!state) return keys;
    const uint32_t b = state->buttons;
    const bool right_trigger_down = state->rtrig >= 0x80;
    if ((b & CONT_A) || right_trigger_down) keys &= ~(1u << 0);
    if (b & CONT_B) keys &= ~(1u << 1);
    if (b & CONT_D) keys &= ~(1u << 2); // Select
    if (b & CONT_START) keys &= ~(1u << 3);
    if (b & CONT_DPAD_RIGHT) keys &= ~(1u << 4);
    if (b & CONT_DPAD_LEFT) keys &= ~(1u << 5);
    if (b & CONT_DPAD_UP) keys &= ~(1u << 6);
    if (b & CONT_DPAD_DOWN) keys &= ~(1u << 7);
    if (b & CONT_Y) keys &= ~(1u << 8); // GBA R
    if (b & CONT_X) keys &= ~(1u << 9); // GBA L
    static bool have_previous = false;
    static bool previous_right_trigger_down = false;
    if (!have_previous ||
        right_trigger_down != previous_right_trigger_down) {
        std::fprintf(stderr,
                     "[dreamcast-input] maple_buttons=0x%08x rtrig=%u keyinput=0x%04x\n",
                     static_cast<unsigned>(b),
                     static_cast<unsigned>(state->rtrig),
                     static_cast<unsigned>(keys));
        previous_right_trigger_down = right_trigger_down;
        have_previous = true;
    }
    return keys;
}
} // namespace

HostWindow::HostWindow() = default;
HostWindow::~HostWindow() { close(); }
bool HostWindow::is_available() { return true; }

bool HostWindow::open(int, int, int, const char*, const char*, bool, bool,
                      bool, bool) {
    vid_set_mode(DM_320x240, PM_RGB565);
    vid_border_color(0, 0, 0);
    open_ = true;
    if (!start_audio_stream())
        std::fprintf(stderr, "[dreamcast-audio] continuing without audio\n");
    std::printf("dreamcast_video=320x240 RGB565, centered GBA 240x160\n");
    return true;
}

void HostWindow::close() {
    stop_audio_stream();
    open_ = false;
}
void HostWindow::set_title(const char*) {}
bool HostWindow::set_surface_size(int, int) { return true; }
bool HostWindow::drawable_size(int* w, int* h) const {
    if (w) *w = kScreenWidth;
    if (h) *h = kScreenHeight;
    return true;
}

void HostWindow::present(const uint8_t* rgb) {
    if (!open_ || !rgb || !vram_s) return;
    static uint32_t present_count = 0;
    ++present_count;
    if (present_count == 1 || (present_count % 60) == 0) {
        std::fprintf(stderr, "[dreamcast-video] presents=%u\n",
                     static_cast<unsigned>(present_count));
        std::fflush(stderr);
    }
    constexpr int x0 = (kScreenWidth - kGameWidth) / 2;
    constexpr int y0 = (kScreenHeight - kGameHeight) / 2;
    vid_waitvbl();
    for (int y = 0; y < kGameHeight; ++y) {
        uint16_t* dst = vram_s + (y + y0) * kScreenWidth + x0;
        const uint8_t* src = rgb + y * kGameWidth * 3;
        for (int x = 0; x < kGameWidth; ++x) {
            const uint8_t r = src[x * 3 + 0];
            const uint8_t g = src[x * 3 + 1];
            const uint8_t b = src[x * 3 + 2];
            dst[x] = static_cast<uint16_t>(((r >> 3) << 11) |
                                           ((g >> 2) << 5) | (b >> 3));
        }
    }
}

void HostWindow::load_input_config(const char*, bool, int) {}
void HostWindow::set_fullscreen(int) {}
int HostWindow::fullscreen() const { return 0; }
void HostWindow::adjust_scale(int) {}
void HostWindow::set_volume(int) {}
int HostWindow::volume() const { return 100; }
int HostWindow::window_scale() const { return 1; }
void HostWindow::set_linear_filter(bool) {}
bool HostWindow::linear_filter() const { return false; }
void HostWindow::set_audio_enabled(bool enabled) {
    if (enabled) start_audio_stream();
    else stop_audio_stream();
}
bool HostWindow::audio_enabled() const {
    return audio_stream != SND_STREAM_INVALID;
}
void HostWindow::set_resize_driven_view(bool) {}
#if defined(GBARECOMP_RUNTIME_UI)
void HostWindow::set_runtime_ui(RecompRuntimeUi*) {}
#endif
void HostWindow::configure_touch(bool, std::uint32_t, int, bool) {}
void HostWindow::set_touch_config_dir(const char*) {}
bool HostWindow::touch_pad_visible() const { return false; }
void HostWindow::set_touch_pad_visible(bool) {}
void HostWindow::set_view_margins(std::uint32_t, std::uint32_t,
                                  std::uint32_t, std::uint32_t) {}
void HostWindow::set_host_overlay(void (*)(HostOverlay*)) {}
float HostWindow::px_per_mm() const { return 1.0f; }
void HostWindow::set_presentation_anchor_top(bool) {}
bool HostWindow::wait_for_foreground() { return true; }
void HostWindow::haptic_pulse(int, float) {}
void HostWindow::set_fps_readout(bool) {}
bool HostWindow::fps_readout() const { return false; }
bool HostWindow::assist_tools_enabled() const { return false; }
int HostWindow::fast_forward_multiplier() const { return 1; }
void HostWindow::push_audio_samples(const int16_t* samples, std::size_t count) {
    if (!samples || audio_stream == SND_STREAM_INVALID || count == 0) return;
    if (count > kAudioRingSamples) {
        samples += count - kAudioRingSamples;
        count = kAudioRingSamples;
    }
    const uint64_t used = audio_head - audio_tail;
    if (used + count > kAudioRingSamples)
        audio_tail = audio_head + count - kAudioRingSamples;
    for (std::size_t i = 0; i < count; ++i) {
        audio_ring[audio_head & (kAudioRingSamples - 1)] = samples[i];
        ++audio_head;
    }
    // The stream callback copies into its aligned buffer synchronously here.
    snd_stream_poll(audio_stream);
}
void host_haptic_pulse(int, float) {}
void host_request_settings_menu() {}
void HostWindow::service_events() {}

HostWindow::Events HostWindow::pump() {
    Events events{};
    events.quit = false;
    events.keyinput = gba_keyinput();
    return events;
}
} // namespace gbarecomp
