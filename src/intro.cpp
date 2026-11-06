#include "intro.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lcd_bsp.h"
#include "fast_tjpgd.h"

namespace {

constexpr char INTRO_FILE[] = "/intro.avi";
constexpr uint32_t MAX_AVI_FRAMES = 512;
constexpr uint32_t JPEG_WORK_BUFFER_SIZE = 16384;
constexpr uint16_t VIDEO_WIDTH = 233;
constexpr uint16_t VIDEO_HEIGHT = 233;

struct AviFrame {
  uint32_t offset;
  uint32_t size;
};

struct AviInfo {
  uint32_t frame_interval_us = 0;
  uint32_t total_frames = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  bool mjpeg_stream = false;
  uint32_t movi_start = 0;
  uint32_t movi_end = 0;
};

struct JpegInput {
  const uint8_t *data;
  uint32_t bytes_remaining;
  uint32_t position;
  uint16_t *pixels;
};

struct DecodeEvent {
  bool done;
  bool preparation_failed;
  uint16_t buffer_index;
  uint32_t frame_index;
  JRESULT result;
  uint32_t prepare_time_us;
  uint32_t decode_time_us;
};

struct DecoderContext {
  const uint8_t *avi_data;
  const AviFrame *frames;
  uint32_t frame_count;
  QueueHandle_t ready_frames;
  SemaphoreHandle_t available_buffers[2];
  uint16_t *frame_buffers[2];
};

uint32_t read_u32(const uint8_t *bytes)
{
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

bool read_at(const uint8_t *data, uint32_t data_size, uint32_t offset,
             uint8_t *buffer, size_t size)
{
  if (offset > data_size || size > data_size - offset) return false;
  std::memcpy(buffer, data + offset, size);
  return true;
}

bool read_chunk_header(const uint8_t *data, uint32_t data_size,
                       uint32_t offset, uint32_t limit,
                       uint8_t header[8], uint32_t &data_end)
{
  if (offset > limit || limit - offset < 8 ||
      !read_at(data, data_size, offset, header, 8)) {
    return false;
  }

  const uint32_t chunk_size = read_u32(header + 4);
  const uint64_t end = static_cast<uint64_t>(offset) + 8 + chunk_size;
  if (end > limit) return false;

  data_end = static_cast<uint32_t>(end);
  return true;
}

bool parse_stream_list(const uint8_t *data, uint32_t data_size,
                       uint32_t start, uint32_t end,
                       AviInfo &info)
{
  bool video_stream = false;
  bool mjpeg_format = false;
  uint16_t stream_width = 0;
  uint16_t stream_height = 0;
  uint32_t position = start;

  while (position < end) {
    uint8_t header[8];
    uint32_t data_end;
    if (!read_chunk_header(data, data_size, position, end, header, data_end)) {
      return false;
    }

    const uint32_t data_start = position + 8;
    const uint32_t chunk_size = data_end - data_start;
    if (std::memcmp(header, "strh", 4) == 0 && chunk_size >= 4) {
      uint8_t stream_type[4];
      if (!read_at(data, data_size, data_start, stream_type,
                   sizeof(stream_type))) {
        return false;
      }
      video_stream = std::memcmp(stream_type, "vids", 4) == 0;
    } else if (std::memcmp(header, "strf", 4) == 0 && chunk_size >= 20) {
      uint8_t bitmap_info[20];
      if (!read_at(data, data_size, data_start, bitmap_info,
                   sizeof(bitmap_info))) {
        return false;
      }
      stream_width = static_cast<uint16_t>(read_u32(bitmap_info + 4));
      stream_height = static_cast<uint16_t>(read_u32(bitmap_info + 8));
      mjpeg_format = std::memcmp(bitmap_info + 16, "MJPG", 4) == 0;
    }

    position = data_end + (chunk_size & 1);
  }

  if (position != end) return false;
  if (video_stream && mjpeg_format) {
    info.width = stream_width;
    info.height = stream_height;
    info.mjpeg_stream = true;
  }
  return true;
}

bool parse_header_list(const uint8_t *data, uint32_t data_size,
                       uint32_t start, uint32_t end,
                       AviInfo &info)
{
  uint32_t position = start;
  while (position < end) {
    uint8_t header[8];
    uint32_t data_end;
    if (!read_chunk_header(data, data_size, position, end, header, data_end)) {
      return false;
    }

    const uint32_t data_start = position + 8;
    const uint32_t chunk_size = data_end - data_start;
    if (std::memcmp(header, "avih", 4) == 0 && chunk_size >= 20) {
      uint8_t main_header[20];
      if (!read_at(data, data_size, data_start, main_header,
                   sizeof(main_header))) {
        return false;
      }
      info.frame_interval_us = read_u32(main_header);
      info.total_frames = read_u32(main_header + 16);
    } else if (std::memcmp(header, "LIST", 4) == 0 && chunk_size >= 4) {
      uint8_t list_type[4];
      if (!read_at(data, data_size, data_start, list_type,
                   sizeof(list_type))) {
        return false;
      }
      if (std::memcmp(list_type, "strl", 4) == 0 &&
          !parse_stream_list(data, data_size, data_start + 4, data_end, info)) {
        return false;
      }
    }

    position = data_end + (chunk_size & 1);
  }

  return position == end;
}

bool collect_video_frames(const uint8_t *data, uint32_t data_size,
                          uint32_t start, uint32_t end,
                          uint8_t depth, AviFrame *frames,
                          uint32_t &frame_count)
{
  if (depth > 4) return false;

  uint32_t position = start;
  while (position < end) {
    uint8_t header[8];
    uint32_t data_end;
    if (!read_chunk_header(data, data_size, position, end, header, data_end)) {
      return false;
    }

    const uint32_t data_start = position + 8;
    const uint32_t chunk_size = data_end - data_start;
    if (std::memcmp(header, "LIST", 4) == 0 && chunk_size >= 4) {
      uint8_t list_type[4];
      if (!read_at(data, data_size, data_start, list_type,
                   sizeof(list_type))) {
        return false;
      }
      if (std::memcmp(list_type, "rec ", 4) == 0 &&
          !collect_video_frames(data, data_size, data_start + 4, data_end,
                                depth + 1, frames, frame_count)) {
        return false;
      }
    } else if ((std::memcmp(header, "00dc", 4) == 0 ||
                std::memcmp(header, "00db", 4) == 0) &&
               chunk_size > 0) {
      if (frame_count == MAX_AVI_FRAMES) return false;
      frames[frame_count++] = {data_start, chunk_size};
    }

    position = data_end + (chunk_size & 1);
  }

  return position == end;
}

bool inspect_avi(const uint8_t *data, uint32_t file_size, AviInfo &info,
                 AviFrame *frames, uint32_t &frame_count)
{
  uint8_t riff_header[12];
  if (file_size < sizeof(riff_header) ||
      !read_at(data, file_size, 0, riff_header, sizeof(riff_header)) ||
      std::memcmp(riff_header, "RIFF", 4) != 0 ||
      std::memcmp(riff_header + 8, "AVI ", 4) != 0) {
    return false;
  }

  const uint64_t riff_end_64 = 8ULL + read_u32(riff_header + 4);
  if (riff_end_64 > file_size) return false;
  const uint32_t riff_end = static_cast<uint32_t>(riff_end_64);

  uint32_t position = 12;
  while (position < riff_end) {
    uint8_t header[8];
    uint32_t data_end;
    if (!read_chunk_header(data, file_size, position, riff_end, header,
                           data_end)) {
      return false;
    }

    const uint32_t data_start = position + 8;
    const uint32_t chunk_size = data_end - data_start;
    if (std::memcmp(header, "LIST", 4) == 0 && chunk_size >= 4) {
      uint8_t list_type[4];
      if (!read_at(data, file_size, data_start, list_type,
                   sizeof(list_type))) {
        return false;
      }
      if (std::memcmp(list_type, "hdrl", 4) == 0 &&
          !parse_header_list(data, file_size, data_start + 4, data_end, info)) {
        return false;
      }
      if (std::memcmp(list_type, "movi", 4) == 0) {
        info.movi_start = data_start + 4;
        info.movi_end = data_end;
      }
    }
    position = data_end + (chunk_size & 1);
  }

  if (position != riff_end || info.frame_interval_us == 0 ||
      info.total_frames == 0 || !info.mjpeg_stream ||
      info.width != VIDEO_WIDTH || info.height != VIDEO_HEIGHT ||
      info.movi_start == 0 || info.movi_end <= info.movi_start) {
    return false;
  }

  return collect_video_frames(data, file_size, info.movi_start,
                              info.movi_end, 0, frames, frame_count) &&
         frame_count > 0 && frame_count == info.total_frames;
}

size_t jpeg_input(JDEC *decoder, uint8_t *buffer, size_t requested)
{
  JpegInput *input = static_cast<JpegInput *>(decoder->device);
  const size_t count = requested < input->bytes_remaining
                           ? requested
                           : input->bytes_remaining;
  if (count == 0) return 0;

  if (buffer != nullptr) {
    std::memcpy(buffer, input->data + input->position, count);
  }
  input->position += count;
  input->bytes_remaining -= count;
  return count;
}

int jpeg_output(JDEC *decoder, void *bitmap, JRECT *rectangle)
{
  JpegInput *input = static_cast<JpegInput *>(decoder->device);
  uint16_t *pixels = input->pixels;
  const uint16_t *source_pixels = static_cast<const uint16_t *>(bitmap);
  const uint16_t rect_width =
      static_cast<uint16_t>(rectangle->right - rectangle->left + 1);
  const uint16_t rect_height =
      static_cast<uint16_t>(rectangle->bottom - rectangle->top + 1);
  if (rectangle->left + rect_width > VIDEO_WIDTH ||
      rectangle->top + rect_height > VIDEO_HEIGHT) {
    return 0;
  }

  for (uint16_t y = 0; y < rect_height; y++) {
    std::memcpy(pixels + (rectangle->top + y) * VIDEO_WIDTH + rectangle->left,
                source_pixels + y * rect_width,
                rect_width * sizeof(uint16_t));
  }

  return 1;
}

void decode_avi_task(void *argument)
{
  DecoderContext *context = static_cast<DecoderContext *>(argument);
  uint8_t *work_buffer = static_cast<uint8_t *>(
      heap_caps_malloc(JPEG_WORK_BUFFER_SIZE, MALLOC_CAP_INTERNAL));
  DecodeEvent event = {};

  if (work_buffer == nullptr) {
    event.done = true;
    event.result = JDR_MEM1;
    xQueueSend(context->ready_frames, &event, portMAX_DELAY);
    vTaskDelete(nullptr);
    return;
  }

  for (uint32_t i = 0; i < context->frame_count; i++) {
    const uint16_t buffer_index = i & 1;
    xSemaphoreTake(context->available_buffers[buffer_index], portMAX_DELAY);
    JpegInput input = {context->avi_data + context->frames[i].offset,
                       context->frames[i].size, 0,
                       context->frame_buffers[buffer_index]};
    JDEC decoder;
    const int64_t prepare_start_us = esp_timer_get_time();
    const JRESULT prepare_result =
        jd_prepare(&decoder, jpeg_input, work_buffer, JPEG_WORK_BUFFER_SIZE,
                   &input);
    const uint32_t prepare_time_us =
        static_cast<uint32_t>(esp_timer_get_time() - prepare_start_us);
    if (prepare_result != JDR_OK || decoder.width != VIDEO_WIDTH ||
        decoder.height != VIDEO_HEIGHT) {
      xSemaphoreGive(context->available_buffers[buffer_index]);
      event = {true, true, buffer_index, i,
               prepare_result == JDR_OK ? JDR_FMT2 : prepare_result,
               prepare_time_us, 0};
      xQueueSend(context->ready_frames, &event, portMAX_DELAY);
      heap_caps_free(work_buffer);
      vTaskDelete(nullptr);
      return;
    }

    const int64_t decode_start_us = esp_timer_get_time();
    const JRESULT decode_result = jd_decomp(&decoder, jpeg_output, 0);
    const uint32_t decode_time_us =
        static_cast<uint32_t>(esp_timer_get_time() - decode_start_us);
    if (decode_result != JDR_OK) {
      xSemaphoreGive(context->available_buffers[buffer_index]);
      event = {true, false, buffer_index, i, decode_result,
               prepare_time_us, decode_time_us};
      xQueueSend(context->ready_frames, &event, portMAX_DELAY);
      heap_caps_free(work_buffer);
      vTaskDelete(nullptr);
      return;
    }

    event = {false, false, buffer_index, i, JDR_OK, prepare_time_us,
             decode_time_us};
    if (xQueueSend(context->ready_frames, &event, portMAX_DELAY) != pdTRUE) {
      xSemaphoreGive(context->available_buffers[buffer_index]);
      break;
    }
  }

  heap_caps_free(work_buffer);
  event = {};
  event.done = true;
  xQueueSend(context->ready_frames, &event, portMAX_DELAY);
  vTaskDelete(nullptr);
}

} // namespace

void play_intro()
{
  const int64_t preparation_start_us = esp_timer_get_time();
  if (!SPIFFS.begin(false, "/spiffs")) {
    Serial.println("Intro skipped: SPIFFS mount failed.");
    return;
  }

  File file = SPIFFS.open(INTRO_FILE, FILE_READ);
  if (!file) {
    Serial.println("Intro skipped: /intro.avi is missing.");
    return;
  }

  static AviFrame frames[MAX_AVI_FRAMES];
  AviInfo info;
  uint32_t frame_count = 0;
  const size_t avi_size = file.size();
  if (avi_size > UINT32_MAX) {
    Serial.println("Intro skipped: AVI file is too large.");
    file.close();
    return;
  }

  const int64_t load_start_us = esp_timer_get_time();
  uint8_t *avi_data = static_cast<uint8_t *>(
      heap_caps_malloc(avi_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (avi_data == nullptr || !file.seek(0) ||
      file.read(avi_data, avi_size) != avi_size) {
    Serial.println("Intro skipped: could not cache AVI in PSRAM.");
    heap_caps_free(avi_data);
    file.close();
    return;
  }
  const int64_t avi_loaded_us = esp_timer_get_time();

  const int64_t inspect_start_us = avi_loaded_us;
  if (!inspect_avi(avi_data, static_cast<uint32_t>(avi_size), info, frames,
                   frame_count)) {
    Serial.println("Intro skipped: invalid or unsupported MJPEG AVI.");
    heap_caps_free(avi_data);
    file.close();
    return;
  }
  const int64_t avi_inspected_us = esp_timer_get_time();

  constexpr size_t frame_buffer_size =
      VIDEO_WIDTH * VIDEO_HEIGHT * sizeof(uint16_t);
  uint16_t *frame_buffer0 = static_cast<uint16_t *>(
      heap_caps_malloc(frame_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  bool frame_buffer0_in_psram = frame_buffer0 != nullptr;
  if (frame_buffer0 == nullptr) {
    frame_buffer0 = static_cast<uint16_t *>(
        heap_caps_malloc(frame_buffer_size,
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  uint16_t *frame_buffer1 = static_cast<uint16_t *>(
      heap_caps_malloc(frame_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  bool frame_buffer1_in_psram = frame_buffer1 != nullptr;
  if (frame_buffer1 == nullptr) {
    frame_buffer1 = static_cast<uint16_t *>(
        heap_caps_malloc(frame_buffer_size,
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (frame_buffer0 == nullptr || frame_buffer1 == nullptr) {
    Serial.println("Intro skipped: not enough memory for frame buffers.");
    heap_caps_free(frame_buffer0);
    heap_caps_free(frame_buffer1);
    heap_caps_free(avi_data);
    file.close();
    return;
  }
  const int64_t buffers_ready_us = esp_timer_get_time();
  Serial.printf("Intro preparation: load %.0f ms, AVI scan %.0f ms, frame buffers %.0f ms, total %.0f ms\n",
                (avi_loaded_us - load_start_us) / 1000.0,
                (avi_inspected_us - inspect_start_us) / 1000.0,
                (buffers_ready_us - avi_inspected_us) / 1000.0,
                (buffers_ready_us - preparation_start_us) / 1000.0);

  DecoderContext context = {};
  context.avi_data = avi_data;
  context.frames = frames;
  context.frame_count = frame_count;
  context.frame_buffers[0] = frame_buffer0;
  context.frame_buffers[1] = frame_buffer1;
  context.ready_frames = xQueueCreate(2, sizeof(DecodeEvent));
  context.available_buffers[0] = xSemaphoreCreateBinary();
  context.available_buffers[1] = xSemaphoreCreateBinary();
  if (context.ready_frames == nullptr ||
      context.available_buffers[0] == nullptr ||
      context.available_buffers[1] == nullptr) {
    Serial.println("Intro skipped: could not create video pipeline.");
    if (context.ready_frames != nullptr) vQueueDelete(context.ready_frames);
    if (context.available_buffers[0] != nullptr) {
      vSemaphoreDelete(context.available_buffers[0]);
    }
    if (context.available_buffers[1] != nullptr) {
      vSemaphoreDelete(context.available_buffers[1]);
    }
    heap_caps_free(frame_buffer0);
    heap_caps_free(frame_buffer1);
    heap_caps_free(avi_data);
    file.close();
    return;
  }
  xSemaphoreGive(context.available_buffers[0]);
  xSemaphoreGive(context.available_buffers[1]);

  const int64_t playback_start_us = esp_timer_get_time();
  int64_t prepare_time_us = 0;
  int64_t decode_time_us = 0;
  uint64_t decode_time_by_buffer_us[2] = {};
  uint32_t decoded_frames_by_buffer[2] = {};
  uint64_t scale_time_us = 0;
  uint64_t transfer_time_us = 0;
  uint32_t displayed_frames = 0;
  bool failed = false;
  TaskHandle_t decoder_task = nullptr;
  if (xTaskCreatePinnedToCore(decode_avi_task, "avi_decode", 8192, &context, 5,
                              &decoder_task, 0) != pdPASS) {
    Serial.println("Intro skipped: could not start decoder task.");
    failed = true;
  } else {
    while (true) {
      DecodeEvent event;
      if (xQueueReceive(context.ready_frames, &event, portMAX_DELAY) != pdTRUE) {
        Serial.println("Intro stopped: video pipeline queue failed.");
        failed = true;
        break;
      }
      if (event.done) {
        if (event.result != JDR_OK) {
          Serial.printf("Intro stopped: JPEG frame %u failed (%d).\n",
                        event.frame_index, event.result);
          failed = true;
        }
        break;
      }

      prepare_time_us += event.prepare_time_us;
      decode_time_us += event.decode_time_us;
      decode_time_by_buffer_us[event.buffer_index] += event.decode_time_us;
      decoded_frames_by_buffer[event.buffer_index]++;
      if (!failed) {
        uint64_t frame_scale_time_us = 0;
        uint64_t frame_transfer_time_us = 0;
        const esp_err_t draw_result = lcd_draw_intro_frame_2x(
            context.frame_buffers[event.buffer_index], &frame_scale_time_us,
            &frame_transfer_time_us);
        scale_time_us += frame_scale_time_us;
        transfer_time_us += frame_transfer_time_us;
        if (draw_result != ESP_OK) {
          Serial.printf("Intro stopped: display transfer failed (%s).\n",
                        esp_err_to_name(draw_result));
          failed = true;
        } else {
          displayed_frames++;
        }
      }
      xSemaphoreGive(context.available_buffers[event.buffer_index]);
    }
  }

  const int64_t elapsed_us = esp_timer_get_time() - playback_start_us;
  if (displayed_frames > 0 && elapsed_us > 0) {
    Serial.printf("Intro: %u/%u frames, %.1f fps%s.\n", displayed_frames,
                  frame_count,
                  displayed_frames * 1000000.0f / elapsed_us,
                  failed ? " (incomplete)" : "");
    Serial.printf("Intro duration: %.2f s (AVI nominal: %.2f s at %.2f fps).\n",
                  elapsed_us / 1000000.0f,
                  frame_count * info.frame_interval_us / 1000000.0f,
                  1000000.0f / info.frame_interval_us);
    Serial.printf("Intro timing/frame: prepare %.2f ms, decode %.2f ms, scale %.2f ms, QSPI %.2f ms.\n",
                  prepare_time_us / (displayed_frames * 1000.0f),
                  decode_time_us / (displayed_frames * 1000.0f),
                  scale_time_us / (displayed_frames * 1000.0f),
                  transfer_time_us / (displayed_frames * 1000.0f));
    for (uint16_t buffer_index = 0; buffer_index < 2; buffer_index++) {
      if (decoded_frames_by_buffer[buffer_index] > 0) {
        Serial.printf("Intro decode buffer %u: %.2f ms/frame (%s RAM).\n",
                      buffer_index,
                      decode_time_by_buffer_us[buffer_index] /
                          (decoded_frames_by_buffer[buffer_index] * 1000.0f),
                      (buffer_index == 0 && frame_buffer0_in_psram) ||
                              (buffer_index == 1 && frame_buffer1_in_psram)
                          ? "PSRAM"
                          : "internal");
      }
    }
  }

  vQueueDelete(context.ready_frames);
  vSemaphoreDelete(context.available_buffers[0]);
  vSemaphoreDelete(context.available_buffers[1]);
  heap_caps_free(frame_buffer0);
  heap_caps_free(frame_buffer1);
  heap_caps_free(avi_data);
  file.close();
}
