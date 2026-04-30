/**
 * device_voice_example.c
 *
 * Agent Linux SDK 设备语音接入示例
 *
 * 作者: hh-zyb
 * 创建日期: 2026年04月13日
 * 更新日期: 2026年04月14日
 * 描述: 面向硬件接入方的最小语音链路开发例程，展示设备鉴权、配对码申请、
 *       WebSocket 建连、流式音频发送以及板端 ALSA 录音/播放的完整调用顺序。
 *
 * 功能:
 *   - 从环境变量读取 ws_url、api_base_url、device_id、device_secret
 *   - 调用 device_secret 相关接口检查设备状态
 *   - 设备未绑定时申请并打印 pairing code
 *   - 设备已绑定时自动获取 device token 并建立已鉴权连接
 *   - 支持 PCM 文件、内置测试帧、ALSA 麦克风采集三种上行方式
 *   - 支持把 TTS 返回的 PCM 音频写入临时文件并通过 `aplay` 从板载扬声器播放
 *   - 打印 session_ready / asr_final / tts_stream_end / assistant_response
 *
 * 环境变量:
 *   - AGENT_WS_URL: WebSocket 服务地址
 *   - AGENT_API_BASE_URL: HTTP API 基础地址，可为空，默认按 ws_url 自动推导
 *   - AGENT_DEVICE_ID: 设备 ID
 *   - AGENT_DEVICE_SECRET: 设备密钥
 *   - AGENT_CLIENT_ID: 客户端 ID，默认 device-voice-example
 *   - AGENT_CA_BUNDLE: 可选，CA 证书文件路径，HTTPS/WSS 联调时建议显式设置
 *   - AGENT_PCM_FILE: 可选，16kHz/单声道/16bit little-endian PCM 文件
 *   - AGENT_ALSA_CAPTURE: 可选，设为 1/true 时启用 arecord 录音
 *   - AGENT_RECORD_SECONDS: 可选，ALSA 固定录音秒数；未设置或 <= 0 时改为边录边发并等待服务端 VAD 截断
 *   - AGENT_MAX_RECORD_SECONDS: 可选，VAD 模式下的本地超时兜底秒数，默认 12
 *   - AGENT_ALSA_CAPTURE_DEVICE: 可选，录音设备名称，默认使用系统默认设备
 *   - AGENT_ALSA_PLAYBACK_DEVICE: 可选，播放设备名称，默认使用系统默认设备
 *   - AGENT_DISABLE_PLAYBACK: 可选，设为 1/true 时不调用 aplay 播放返回音频
 *   - AGENT_TTS_SAMPLE_RATE: 可选，播放返回 PCM 的采样率，默认 16000
 *   - AGENT_WAIT_SECONDS: 等待服务端响应秒数，默认 30
 *   - AGENT_CONNECT_RETRY_COUNT: 可选，公网 WSS 初始化失败时的重试次数，默认 3
 *   - AGENT_CONNECT_RETRY_DELAY_MS: 可选，公网 WSS 初始化失败后的重试间隔毫秒，默认 1000
 *
 * 编译:
 *   gcc -o device_voice_example device_voice_example.c \
 *       -I../include -L../lib -lagent_sdk -lpthread -lwebsockets -lcurl
 *
 * 运行:
 *   AGENT_WS_URL=wss://example.com/ws/v1/chat \
 *   AGENT_API_BASE_URL=https://example.com \
 *   AGENT_DEVICE_ID=0001 \
 *   AGENT_DEVICE_SECRET=xxxx \
 *   AGENT_ALSA_CAPTURE=1 \
 *   ./device_voice_example
 */

#include "agent_sdk.h"

#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define PCM_SAMPLE_RATE 16000
#define PCM_CHANNELS 1
#define PCM_SAMPLE_WIDTH_BYTES 2
#define PCM_FRAME_DURATION_MS 20
#define PCM_FRAME_BYTES ((PCM_SAMPLE_RATE * PCM_CHANNELS * PCM_SAMPLE_WIDTH_BYTES * PCM_FRAME_DURATION_MS) / 1000)
#define SYNTHETIC_FRAME_COUNT 60
#define DEFAULT_RECORD_SECONDS 5
#define DEFAULT_MAX_RECORD_SECONDS 12
#define DEFAULT_CONNECT_RETRY_COUNT 3
#define DEFAULT_CONNECT_RETRY_DELAY_MS 1000

static atomic_int g_session_ready_received_flag = 0;
static atomic_int g_audio_stream_started_flag = 0;
static atomic_int g_audio_stream_endpoint_detected_flag = 0;
static atomic_int g_asr_final_received_flag = 0;
static atomic_int g_tts_stream_finished_flag = 0;
static atomic_int g_assistant_response_received_flag = 0;
static atomic_int g_audio_callback_received_flag = 0;
static atomic_int g_tts_output_write_failed_flag = 0;
static atomic_int g_tts_stream_playback_enabled_flag = 0;
static atomic_int g_tts_stream_playback_started_flag = 0;
static atomic_int g_tts_stream_playback_failed_flag = 0;
static atomic_long g_audio_bytes_received_count = 0;

static FILE *g_tts_output_file = NULL;
static char g_tts_output_path[PATH_MAX] = {0};
static pthread_mutex_t g_aplay_stream_mutex = PTHREAD_MUTEX_INITIALIZER;
static pid_t g_aplay_stream_pid = -1;
static int g_aplay_stream_stdin_fd = -1;
static int g_aplay_stream_sample_rate = PCM_SAMPLE_RATE;
static char g_aplay_stream_device_name[128] = {0};
static char g_aplay_stream_error_message[256] = {0};

typedef struct ArecordStream {
    pid_t m_pid;
    int m_stdout_fd;
} ArecordStream;

static int readOptionalInt(const char *f_env_name, int f_default_value) {
    const char *t_value_text = getenv(f_env_name);
    int t_value = 0;

    if (t_value_text == NULL || t_value_text[0] == '\0') {
        return f_default_value;
    }
    t_value = atoi(t_value_text);
    if (t_value <= 0) {
        return f_default_value;
    }
    return t_value;
}

static double getMonotonicSeconds(void) {
    struct timespec t_now = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &t_now) != 0) {
        return 0.0;
    }
    return (double)t_now.tv_sec + ((double)t_now.tv_nsec / 1000000000.0);
}

static int readWaitIterationCount(void) {
    return readOptionalInt("AGENT_WAIT_SECONDS", 30) * 10;
}

static int readOptionalBool(const char *f_env_name) {
    const char *t_value_text = getenv(f_env_name);

    if (t_value_text == NULL || t_value_text[0] == '\0') {
        return 0;
    }
    if (
            strcmp(t_value_text, "1") == 0
            || strcmp(t_value_text, "true") == 0
            || strcmp(t_value_text, "TRUE") == 0
            || strcmp(t_value_text, "yes") == 0
            || strcmp(t_value_text, "YES") == 0
            || strcmp(t_value_text, "on") == 0
            || strcmp(t_value_text, "ON") == 0
    ) {
        return 1;
    }
    return 0;
}

static const char *readRequiredEnv(const char *f_env_name) {
    const char *t_value = getenv(f_env_name);
    if (t_value == NULL || t_value[0] == '\0') {
        fprintf(stderr, "[device_voice_example] missing env: %s\n", f_env_name);
        return NULL;
    }
    return t_value;
}

static const char *readOptionalEnv(const char *f_env_name, const char *f_default_value) {
    const char *t_value = getenv(f_env_name);
    if (t_value == NULL || t_value[0] == '\0') {
        return f_default_value;
    }
    return t_value;
}

static int createTempFilePath(const char *f_prefix, char *f_output_path, size_t f_output_path_size) {
    int t_fd = -1;

    if (f_prefix == NULL || f_output_path == NULL || f_output_path_size < 32U) {
        return -1;
    }
    if (snprintf(f_output_path, f_output_path_size, "/tmp/%s_XXXXXX", f_prefix) >= (int)f_output_path_size) {
        return -1;
    }
    t_fd = mkstemp(f_output_path);
    if (t_fd < 0) {
        fprintf(stderr, "[device_voice_example] mkstemp failed: %s\n", strerror(errno));
        return -1;
    }
    close(t_fd);
    return 0;
}

static int runProcessAndWait(char *const f_argv[]) {
    pid_t t_pid = 0;
    int t_status = 0;

    if (f_argv == NULL || f_argv[0] == NULL) {
        return -1;
    }

    t_pid = fork();
    if (t_pid < 0) {
        fprintf(stderr, "[device_voice_example] fork failed: %s\n", strerror(errno));
        return -1;
    }
    if (t_pid == 0) {
        execvp(f_argv[0], f_argv);
        fprintf(stderr, "[device_voice_example] exec %s failed: %s\n", f_argv[0], strerror(errno));
        _exit(127);
    }

    if (waitpid(t_pid, &t_status, 0) < 0) {
        fprintf(stderr, "[device_voice_example] waitpid failed: %s\n", strerror(errno));
        return -1;
    }
    if (WIFEXITED(t_status) && WEXITSTATUS(t_status) == 0) {
        return 0;
    }
    if (WIFEXITED(t_status)) {
        fprintf(stderr, "[device_voice_example] process %s exit code=%d\n", f_argv[0], WEXITSTATUS(t_status));
        return -1;
    }
    if (WIFSIGNALED(t_status)) {
        fprintf(stderr, "[device_voice_example] process %s terminated by signal=%d\n", f_argv[0], WTERMSIG(t_status));
        return -1;
    }
    fprintf(stderr, "[device_voice_example] process %s ended unexpectedly\n", f_argv[0]);
    return -1;
}

static int recordPcmWithArecord(const char *f_output_path, int f_record_seconds, const char *f_capture_device) {
    char t_record_seconds_text[16] = {0};
    char *t_argv[16] = {0};
    int t_arg_index = 0;

    if (f_output_path == NULL || f_output_path[0] == '\0') {
        return -1;
    }

    snprintf(t_record_seconds_text, sizeof(t_record_seconds_text), "%d", f_record_seconds);
    t_argv[t_arg_index++] = "arecord";
    if (f_capture_device != NULL && f_capture_device[0] != '\0') {
        t_argv[t_arg_index++] = "-D";
        t_argv[t_arg_index++] = (char *)f_capture_device;
    }
    t_argv[t_arg_index++] = "-q";
    t_argv[t_arg_index++] = "-t";
    t_argv[t_arg_index++] = "raw";
    t_argv[t_arg_index++] = "-f";
    t_argv[t_arg_index++] = "S16_LE";
    t_argv[t_arg_index++] = "-r";
    t_argv[t_arg_index++] = "16000";
    t_argv[t_arg_index++] = "-c";
    t_argv[t_arg_index++] = "1";
    t_argv[t_arg_index++] = "-d";
    t_argv[t_arg_index++] = t_record_seconds_text;
    t_argv[t_arg_index++] = (char *)f_output_path;

    printf(
            "[device_voice_example] start arecord: seconds=%d device=%s output=%s\n",
            f_record_seconds,
            (f_capture_device != NULL && f_capture_device[0] != '\0') ? f_capture_device : "<default>",
            f_output_path
    );
    return runProcessAndWait(t_argv);
}

static int startArecordStream(ArecordStream *f_stream, const char *f_capture_device) {
    int t_pipe_fd[2] = {-1, -1};
    pid_t t_pid = -1;

    if (f_stream == NULL) {
        return -1;
    }
    f_stream->m_pid = -1;
    f_stream->m_stdout_fd = -1;

    if (pipe(t_pipe_fd) != 0) {
        fprintf(stderr, "[device_voice_example] arecord pipe failed: %s\n", strerror(errno));
        return -1;
    }

    t_pid = fork();
    if (t_pid < 0) {
        fprintf(stderr, "[device_voice_example] arecord fork failed: %s\n", strerror(errno));
        close(t_pipe_fd[0]);
        close(t_pipe_fd[1]);
        return -1;
    }

    if (t_pid == 0) {
        char *t_argv[16] = {0};
        int t_arg_index = 0;

        if (dup2(t_pipe_fd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(t_pipe_fd[0]);
        close(t_pipe_fd[1]);

        t_argv[t_arg_index++] = "arecord";
        if (f_capture_device != NULL && f_capture_device[0] != '\0') {
            t_argv[t_arg_index++] = "-D";
            t_argv[t_arg_index++] = (char *)f_capture_device;
        }
        t_argv[t_arg_index++] = "-q";
        t_argv[t_arg_index++] = "-t";
        t_argv[t_arg_index++] = "raw";
        t_argv[t_arg_index++] = "-f";
        t_argv[t_arg_index++] = "S16_LE";
        t_argv[t_arg_index++] = "-r";
        t_argv[t_arg_index++] = "16000";
        t_argv[t_arg_index++] = "-c";
        t_argv[t_arg_index++] = "1";
        execvp(t_argv[0], t_argv);
        _exit(127);
    }

    close(t_pipe_fd[1]);
    f_stream->m_pid = t_pid;
    f_stream->m_stdout_fd = t_pipe_fd[0];
    return 0;
}

static void stopArecordStream(ArecordStream *f_stream) {
    if (f_stream == NULL) {
        return;
    }
    if (f_stream->m_stdout_fd >= 0) {
        close(f_stream->m_stdout_fd);
        f_stream->m_stdout_fd = -1;
    }
    if (f_stream->m_pid > 0) {
        kill(f_stream->m_pid, SIGTERM);
        (void)waitpid(f_stream->m_pid, NULL, 0);
        f_stream->m_pid = -1;
    }
}

static int waitForAudioStreamStarted(int f_timeout_ms) {
    double t_deadline = getMonotonicSeconds() + ((double)f_timeout_ms / 1000.0);

    while (getMonotonicSeconds() < t_deadline) {
        if (atomic_load(&g_audio_stream_started_flag) != 0) {
            return 0;
        }
        usleep(50000);
    }
    return -1;
}

static int playPcmWithAplay(const char *f_input_path, int f_sample_rate, const char *f_playback_device) {
    char t_sample_rate_text[16] = {0};
    char *t_argv[16] = {0};
    int t_arg_index = 0;

    if (f_input_path == NULL || f_input_path[0] == '\0') {
        return -1;
    }

    snprintf(t_sample_rate_text, sizeof(t_sample_rate_text), "%d", f_sample_rate);
    t_argv[t_arg_index++] = "aplay";
    if (f_playback_device != NULL && f_playback_device[0] != '\0') {
        t_argv[t_arg_index++] = "-D";
        t_argv[t_arg_index++] = (char *)f_playback_device;
    }
    t_argv[t_arg_index++] = "-q";
    t_argv[t_arg_index++] = "-t";
    t_argv[t_arg_index++] = "raw";
    t_argv[t_arg_index++] = "-f";
    t_argv[t_arg_index++] = "S16_LE";
    t_argv[t_arg_index++] = "-r";
    t_argv[t_arg_index++] = t_sample_rate_text;
    t_argv[t_arg_index++] = "-c";
    t_argv[t_arg_index++] = "1";
    t_argv[t_arg_index++] = (char *)f_input_path;

    printf(
            "[device_voice_example] start aplay: sample_rate=%d device=%s input=%s\n",
            f_sample_rate,
            (f_playback_device != NULL && f_playback_device[0] != '\0') ? f_playback_device : "<default>",
            f_input_path
    );
    return runProcessAndWait(t_argv);
}

static void resetAplayStreamState(int f_sample_rate, const char *f_playback_device) {
    pthread_mutex_lock(&g_aplay_stream_mutex);
    g_aplay_stream_pid = -1;
    g_aplay_stream_stdin_fd = -1;
    g_aplay_stream_sample_rate = f_sample_rate > 0 ? f_sample_rate : PCM_SAMPLE_RATE;
    g_aplay_stream_error_message[0] = '\0';
    if (f_playback_device != NULL && f_playback_device[0] != '\0') {
        snprintf(g_aplay_stream_device_name, sizeof(g_aplay_stream_device_name), "%s", f_playback_device);
    } else {
        g_aplay_stream_device_name[0] = '\0';
    }
    pthread_mutex_unlock(&g_aplay_stream_mutex);
    atomic_store(&g_tts_stream_playback_started_flag, 0);
    atomic_store(&g_tts_stream_playback_failed_flag, 0);
}

static void setAplayStreamErrorLocked(const char *f_error_message) {
    if (f_error_message == NULL) {
        g_aplay_stream_error_message[0] = '\0';
        return;
    }
    snprintf(g_aplay_stream_error_message, sizeof(g_aplay_stream_error_message), "%s", f_error_message);
}

static int startAplayStreamIfNeeded(void) {
    int t_pipe_fd[2] = {-1, -1};
    char t_sample_rate_text[16] = {0};
    pid_t t_pid = -1;
    int t_status = 0;

    pthread_mutex_lock(&g_aplay_stream_mutex);
    if (g_aplay_stream_stdin_fd >= 0) {
        pthread_mutex_unlock(&g_aplay_stream_mutex);
        return 0;
    }
    if (atomic_load(&g_tts_stream_playback_failed_flag) != 0) {
        pthread_mutex_unlock(&g_aplay_stream_mutex);
        return -1;
    }
    if (pipe(t_pipe_fd) != 0) {
        setAplayStreamErrorLocked(strerror(errno));
        atomic_store(&g_tts_stream_playback_failed_flag, 1);
        pthread_mutex_unlock(&g_aplay_stream_mutex);
        return -1;
    }

    t_pid = fork();
    if (t_pid < 0) {
        setAplayStreamErrorLocked(strerror(errno));
        atomic_store(&g_tts_stream_playback_failed_flag, 1);
        close(t_pipe_fd[0]);
        close(t_pipe_fd[1]);
        pthread_mutex_unlock(&g_aplay_stream_mutex);
        return -1;
    }

    if (t_pid == 0) {
        char *t_argv[16] = {0};
        int t_arg_index = 0;

        snprintf(t_sample_rate_text, sizeof(t_sample_rate_text), "%d", g_aplay_stream_sample_rate);
        if (dup2(t_pipe_fd[0], STDIN_FILENO) < 0) {
            _exit(127);
        }
        close(t_pipe_fd[0]);
        close(t_pipe_fd[1]);

        t_argv[t_arg_index++] = "aplay";
        if (g_aplay_stream_device_name[0] != '\0') {
            t_argv[t_arg_index++] = "-D";
            t_argv[t_arg_index++] = g_aplay_stream_device_name;
        }
        t_argv[t_arg_index++] = "-q";
        t_argv[t_arg_index++] = "-t";
        t_argv[t_arg_index++] = "raw";
        t_argv[t_arg_index++] = "-f";
        t_argv[t_arg_index++] = "S16_LE";
        t_argv[t_arg_index++] = "-r";
        t_argv[t_arg_index++] = t_sample_rate_text;
        t_argv[t_arg_index++] = "-c";
        t_argv[t_arg_index++] = "1";
        t_argv[t_arg_index++] = "-";
        execvp(t_argv[0], t_argv);
        _exit(127);
    }

    close(t_pipe_fd[0]);
    g_aplay_stream_pid = t_pid;
    g_aplay_stream_stdin_fd = t_pipe_fd[1];
    atomic_store(&g_tts_stream_playback_started_flag, 1);
    pthread_mutex_unlock(&g_aplay_stream_mutex);

    usleep(50000);
    if (waitpid(t_pid, &t_status, WNOHANG) == t_pid) {
        pthread_mutex_lock(&g_aplay_stream_mutex);
        if (g_aplay_stream_stdin_fd >= 0) {
            close(g_aplay_stream_stdin_fd);
            g_aplay_stream_stdin_fd = -1;
        }
        g_aplay_stream_pid = -1;
        if (WIFEXITED(t_status)) {
            snprintf(
                    g_aplay_stream_error_message,
                    sizeof(g_aplay_stream_error_message),
                    "aplay exited early: exit_code=%d",
                    WEXITSTATUS(t_status)
            );
        } else if (WIFSIGNALED(t_status)) {
            snprintf(
                    g_aplay_stream_error_message,
                    sizeof(g_aplay_stream_error_message),
                    "aplay exited early: signal=%d",
                    WTERMSIG(t_status)
            );
        } else {
            snprintf(g_aplay_stream_error_message, sizeof(g_aplay_stream_error_message), "%s", "aplay exited early");
        }
        atomic_store(&g_tts_stream_playback_failed_flag, 1);
        pthread_mutex_unlock(&g_aplay_stream_mutex);
        return -1;
    }

    printf(
            "[device_voice_example] start aplay stream: sample_rate=%d device=%s\n",
            g_aplay_stream_sample_rate,
            g_aplay_stream_device_name[0] != '\0' ? g_aplay_stream_device_name : "<default>"
    );
    return 0;
}

static int writeAplayStream(const unsigned char *f_audio_data, size_t f_audio_size) {
    size_t t_written_total = 0U;

    if (f_audio_data == NULL || f_audio_size == 0U) {
        return 0;
    }
    if (startAplayStreamIfNeeded() != 0) {
        return -1;
    }

    pthread_mutex_lock(&g_aplay_stream_mutex);
    while (t_written_total < f_audio_size && g_aplay_stream_stdin_fd >= 0) {
        ssize_t t_written_size = write(
                g_aplay_stream_stdin_fd,
                f_audio_data + t_written_total,
                f_audio_size - t_written_total
        );
        if (t_written_size < 0) {
            setAplayStreamErrorLocked(strerror(errno));
            atomic_store(&g_tts_stream_playback_failed_flag, 1);
            pthread_mutex_unlock(&g_aplay_stream_mutex);
            return -1;
        }
        t_written_total += (size_t)t_written_size;
    }
    pthread_mutex_unlock(&g_aplay_stream_mutex);
    return 0;
}

static int finishAplayStream(void) {
    pid_t t_pid = -1;
    int t_status = 0;

    pthread_mutex_lock(&g_aplay_stream_mutex);
    if (g_aplay_stream_stdin_fd >= 0) {
        close(g_aplay_stream_stdin_fd);
        g_aplay_stream_stdin_fd = -1;
    }
    t_pid = g_aplay_stream_pid;
    g_aplay_stream_pid = -1;
    pthread_mutex_unlock(&g_aplay_stream_mutex);

    if (t_pid <= 0) {
        return atomic_load(&g_tts_stream_playback_failed_flag) ? -1 : 0;
    }
    if (waitpid(t_pid, &t_status, 0) < 0) {
        pthread_mutex_lock(&g_aplay_stream_mutex);
        setAplayStreamErrorLocked(strerror(errno));
        pthread_mutex_unlock(&g_aplay_stream_mutex);
        atomic_store(&g_tts_stream_playback_failed_flag, 1);
        return -1;
    }
    if (WIFEXITED(t_status) && WEXITSTATUS(t_status) == 0) {
        printf("[device_voice_example] aplay stream finished\n");
        return 0;
    }

    pthread_mutex_lock(&g_aplay_stream_mutex);
    if (WIFEXITED(t_status)) {
        snprintf(
                g_aplay_stream_error_message,
                sizeof(g_aplay_stream_error_message),
                "aplay exit_code=%d",
                WEXITSTATUS(t_status)
        );
    } else if (WIFSIGNALED(t_status)) {
        snprintf(
                g_aplay_stream_error_message,
                sizeof(g_aplay_stream_error_message),
                "aplay signal=%d",
                WTERMSIG(t_status)
        );
    } else {
        snprintf(g_aplay_stream_error_message, sizeof(g_aplay_stream_error_message), "%s", "aplay ended unexpectedly");
    }
    pthread_mutex_unlock(&g_aplay_stream_mutex);
    atomic_store(&g_tts_stream_playback_failed_flag, 1);
    return -1;
}

static void abortAplayStream(void) {
    pid_t t_pid = -1;

    pthread_mutex_lock(&g_aplay_stream_mutex);
    if (g_aplay_stream_stdin_fd >= 0) {
        close(g_aplay_stream_stdin_fd);
        g_aplay_stream_stdin_fd = -1;
    }
    t_pid = g_aplay_stream_pid;
    g_aplay_stream_pid = -1;
    pthread_mutex_unlock(&g_aplay_stream_mutex);

    if (t_pid > 0) {
        kill(t_pid, SIGTERM);
        (void)waitpid(t_pid, NULL, 0);
    }
}

static int prepareTtsOutputFile(void) {
    if (createTempFilePath("device_voice_tts", g_tts_output_path, sizeof(g_tts_output_path)) != 0) {
        return -1;
    }
    g_tts_output_file = fopen(g_tts_output_path, "wb");
    if (g_tts_output_file == NULL) {
        fprintf(stderr, "[device_voice_example] open tts output file failed: %s\n", g_tts_output_path);
        g_tts_output_path[0] = '\0';
        return -1;
    }
    printf("[device_voice_example] tts output file: %s\n", g_tts_output_path);
    return 0;
}

static void closeTtsOutputFile(void) {
    if (g_tts_output_file == NULL) {
        return;
    }
    fflush(g_tts_output_file);
    fclose(g_tts_output_file);
    g_tts_output_file = NULL;
}

static void printActivationStatus(const AgentDeviceActivationStatus *f_status) {
    printf(
            "[device_voice_example] activation: device_id=%s status=%s setup_status=%s activated=%d bound=%d can_request_pairing_code=%d current_agent_id=%s\n",
            f_status->m_device_id,
            f_status->m_status,
            f_status->m_setup_status,
            f_status->m_activated_flag,
            f_status->m_bound_flag,
            f_status->m_can_request_pairing_code_flag,
            f_status->m_current_agent_id
    );
}

static int printPairingCodeIfNeeded(AgentClient *f_client, const AgentDeviceActivationStatus *f_status) {
    AgentDevicePairingCodeResult t_pairing_result = {0};
    int t_result = 0;

    if (f_status->m_can_request_pairing_code_flag == 0) {
        fprintf(stderr, "[device_voice_example] device not activated/bound and pairing code cannot be requested now\n");
        return AGENT_ERR_DEVICE_NOT_ACTIVATED;
    }

    t_result = agentFetchDevicePairingCode(f_client, &t_pairing_result);
    if (t_result != AGENT_OK) {
        fprintf(stderr, "[device_voice_example] agentFetchDevicePairingCode failed: %d\n", t_result);
        return t_result;
    }

    printf(
            "[device_voice_example] pairing_code: code=%s expires_at=%s scene=%s status=%s\n",
            t_pairing_result.m_pairing_code,
            t_pairing_result.m_pairing_expires_at,
            t_pairing_result.m_scene,
            t_pairing_result.m_status
    );
    printf("[device_voice_example] device is not bound, please complete pairing in Web/App first\n");
    return AGENT_OK;
}

static int ensureAuthorizedConnectionWithRetry(
        AgentClient *f_client,
        AgentDeviceActivationStatus *f_status,
        AgentDeviceTokenResult *f_token
) {
    int t_retry_count = readOptionalInt("AGENT_CONNECT_RETRY_COUNT", DEFAULT_CONNECT_RETRY_COUNT);
    int t_retry_delay_ms = readOptionalInt("AGENT_CONNECT_RETRY_DELAY_MS", DEFAULT_CONNECT_RETRY_DELAY_MS);
    int t_attempt_index = 0;
    int t_result = AGENT_ERR_INTERNAL;

    if (f_client == NULL || f_status == NULL || f_token == NULL) {
        return AGENT_ERR_INVALID_PARAM;
    }
    if (t_retry_count <= 0) {
        t_retry_count = DEFAULT_CONNECT_RETRY_COUNT;
    }
    if (t_retry_delay_ms < 0) {
        t_retry_delay_ms = DEFAULT_CONNECT_RETRY_DELAY_MS;
    }

    for (t_attempt_index = 1; t_attempt_index <= t_retry_count; t_attempt_index++) {
        t_result = agentEnsureAuthorizedConnection(f_client, f_status, f_token);
        if (t_result == AGENT_OK) {
            if (t_attempt_index > 1) {
                printf(
                        "[device_voice_example] agentEnsureAuthorizedConnection recovered on attempt %d/%d\n",
                        t_attempt_index,
                        t_retry_count
                );
            }
            return AGENT_OK;
        }

        fprintf(
                stderr,
                "[device_voice_example] agentEnsureAuthorizedConnection attempt %d/%d failed: %d\n",
                t_attempt_index,
                t_retry_count,
                t_result
        );
        if (t_result != AGENT_ERR_WS_INIT && t_result != AGENT_ERR_TIMEOUT) {
            return t_result;
        }
        (void)agentDisconnect(f_client);
        if (t_attempt_index < t_retry_count && t_retry_delay_ms > 0) {
            usleep((useconds_t)t_retry_delay_ms * 1000U);
        }
    }

    return t_result;
}

static int loadPcmFile(const char *f_file_path, unsigned char **f_pcm_bytes, size_t *f_pcm_size) {
    FILE *t_file = NULL;
    long t_file_size = 0;
    unsigned char *t_buffer = NULL;
    size_t t_read_size = 0U;

    if (f_file_path == NULL || f_pcm_bytes == NULL || f_pcm_size == NULL) {
        return -1;
    }

    t_file = fopen(f_file_path, "rb");
    if (t_file == NULL) {
        fprintf(stderr, "[device_voice_example] open pcm file failed: %s\n", f_file_path);
        return -1;
    }
    if (fseek(t_file, 0L, SEEK_END) != 0) {
        fclose(t_file);
        return -1;
    }
    t_file_size = ftell(t_file);
    if (t_file_size <= 0L) {
        fclose(t_file);
        return -1;
    }
    if (fseek(t_file, 0L, SEEK_SET) != 0) {
        fclose(t_file);
        return -1;
    }

    t_buffer = (unsigned char *)malloc((size_t)t_file_size);
    if (t_buffer == NULL) {
        fclose(t_file);
        return -1;
    }

    t_read_size = fread(t_buffer, 1U, (size_t)t_file_size, t_file);
    fclose(t_file);
    if (t_read_size != (size_t)t_file_size) {
        free(t_buffer);
        return -1;
    }

    *f_pcm_bytes = t_buffer;
    *f_pcm_size = t_read_size;
    return 0;
}

static void fillSyntheticFrame(unsigned char *f_frame, int f_frame_index) {
    size_t t_byte_index = 0U;
    unsigned char t_positive_value = 0x20U;
    unsigned char t_negative_value = 0xE0U;

    for (t_byte_index = 0U; t_byte_index + 1U < PCM_FRAME_BYTES; t_byte_index += 2U) {
        if (((int)(t_byte_index / 2U) + f_frame_index) % 16 < 8) {
            f_frame[t_byte_index] = t_positive_value;
            f_frame[t_byte_index + 1U] = 0x00U;
            continue;
        }
        f_frame[t_byte_index] = t_negative_value;
        f_frame[t_byte_index + 1U] = 0xFFU;
    }
}

static int sendSyntheticAudioStream(AgentClient *f_client) {
    unsigned char t_frame[PCM_FRAME_BYTES] = {0};
    int t_frame_index = 0;

    if (agentStartAudioStream(f_client) != AGENT_OK) {
        return AGENT_ERR_SEND_FAILED;
    }
    for (t_frame_index = 0; t_frame_index < SYNTHETIC_FRAME_COUNT; t_frame_index++) {
        fillSyntheticFrame(t_frame, t_frame_index);
        if (agentSendAudioChunk(f_client, t_frame, sizeof(t_frame)) != AGENT_OK) {
            (void)agentCancelAudioStream(f_client);
            return AGENT_ERR_SEND_FAILED;
        }
        usleep(PCM_FRAME_DURATION_MS * 1000);
    }
    return agentFinishAudioStream(f_client);
}

static int sendPcmAudioStream(AgentClient *f_client, const unsigned char *f_pcm_bytes, size_t f_pcm_size) {
    size_t t_offset = 0U;

    if (agentStartAudioStream(f_client) != AGENT_OK) {
        return AGENT_ERR_SEND_FAILED;
    }
    while (t_offset < f_pcm_size) {
        size_t t_chunk_size = PCM_FRAME_BYTES;
        if (t_offset + t_chunk_size > f_pcm_size) {
            t_chunk_size = f_pcm_size - t_offset;
        }
        if (agentSendAudioChunk(f_client, f_pcm_bytes + t_offset, t_chunk_size) != AGENT_OK) {
            (void)agentCancelAudioStream(f_client);
            return AGENT_ERR_SEND_FAILED;
        }
        t_offset += t_chunk_size;
        usleep(PCM_FRAME_DURATION_MS * 1000);
    }
    return agentFinishAudioStream(f_client);
}

static int sendLiveArecordAudioStream(
        AgentClient *f_client,
        const char *f_capture_device,
        int f_record_until_endpoint_flag,
        int f_record_seconds,
        int f_max_record_seconds
) {
    ArecordStream t_stream = {0};
    unsigned char t_chunk[PCM_FRAME_BYTES] = {0};
    double t_deadline = 0.0;
    long t_recorded_pcm_bytes = 0L;

    if (agentStartAudioStream(f_client) != AGENT_OK) {
        return AGENT_ERR_SEND_FAILED;
    }
    if (waitForAudioStreamStarted(10000) != 0) {
        fprintf(stderr, "[device_voice_example] wait audio_stream_started timeout\n");
        (void)agentCancelAudioStream(f_client);
        return AGENT_ERR_SEND_FAILED;
    }

    if (startArecordStream(&t_stream, f_capture_device) != 0) {
        (void)agentCancelAudioStream(f_client);
        return AGENT_ERR_SEND_FAILED;
    }

    if (f_record_until_endpoint_flag != 0) {
        t_deadline = getMonotonicSeconds() + (double)f_max_record_seconds;
        printf("[device_voice_example] recording_until_endpoint=true\n");
        printf("[device_voice_example] max_record_seconds=%d\n", f_max_record_seconds);
        printf("[device_voice_example] 已开始录音，请讲话；服务端检测到停讲或本地超时后会自动结束本轮录音。\n");
    } else {
        t_deadline = getMonotonicSeconds() + (double)f_record_seconds;
        printf("[device_voice_example] recording_seconds=%d\n", f_record_seconds);
    }

    while (1) {
        ssize_t t_read_size = 0;

        if (f_record_until_endpoint_flag != 0 && atomic_load(&g_audio_stream_endpoint_detected_flag) != 0) {
            break;
        }
        if (getMonotonicSeconds() >= t_deadline) {
            printf("[device_voice_example] local_record_timeout_reached\n");
            break;
        }

        t_read_size = read(t_stream.m_stdout_fd, t_chunk, sizeof(t_chunk));
        if (t_read_size < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "[device_voice_example] read arecord stream failed: %s\n", strerror(errno));
            stopArecordStream(&t_stream);
            (void)agentCancelAudioStream(f_client);
            return AGENT_ERR_SEND_FAILED;
        }
        if (t_read_size == 0) {
            break;
        }

        t_recorded_pcm_bytes += t_read_size;
        if (agentSendAudioChunk(f_client, t_chunk, (size_t)t_read_size) != AGENT_OK) {
            stopArecordStream(&t_stream);
            (void)agentCancelAudioStream(f_client);
            return AGENT_ERR_SEND_FAILED;
        }
    }

    stopArecordStream(&t_stream);
    printf("[device_voice_example] recorded_pcm_bytes=%ld\n", t_recorded_pcm_bytes);
    return agentFinishAudioStream(f_client);
}

static int waitForConversationResult(int f_wait_for_tts_completion_flag) {
    int t_retry_index = 0;

    for (t_retry_index = 0; t_retry_index < readWaitIterationCount(); t_retry_index++) {
        if (f_wait_for_tts_completion_flag != 0) {
            if (atomic_load(&g_tts_stream_finished_flag)) {
                return 0;
            }
        } else if (
                atomic_load(&g_assistant_response_received_flag)
                || (
                        atomic_load(&g_tts_stream_finished_flag)
                        && atomic_load(&g_audio_callback_received_flag)
                )
        ) {
            return 0;
        }
        usleep(100000);
    }
    return -1;
}

static void onMessage(const char *f_message, void *f_user_data) {
    (void)f_user_data;
    printf("[device_voice_example] message: %s\n", f_message == NULL ? "" : f_message);
    if (f_message != NULL && strstr(f_message, "\"type\":\"session_ready\"") != NULL) {
        atomic_store(&g_session_ready_received_flag, 1);
    }
    if (f_message != NULL && strstr(f_message, "\"type\":\"audio_stream_started\"") != NULL) {
        atomic_store(&g_audio_stream_started_flag, 1);
        return;
    }
    if (f_message != NULL && strstr(f_message, "\"type\":\"audio_stream_endpoint_detected\"") != NULL) {
        atomic_store(&g_audio_stream_endpoint_detected_flag, 1);
        return;
    }
    if (f_message != NULL && strstr(f_message, "\"type\":\"asr_final\"") != NULL) {
        atomic_store(&g_asr_final_received_flag, 1);
    }
    if (f_message != NULL && strstr(f_message, "\"type\":\"tts_stream_end\"") != NULL) {
        atomic_store(&g_tts_stream_finished_flag, 1);
    }
    if (f_message != NULL && strstr(f_message, "\"type\":\"assistant_response\"") != NULL) {
        atomic_store(&g_assistant_response_received_flag, 1);
    }
}

static void onStatus(AgentStatus f_status, void *f_user_data) {
    (void)f_user_data;
    printf("[device_voice_example] status: %d\n", (int)f_status);
}

static void onAudio(const unsigned char *f_audio_data, size_t f_audio_size, void *f_user_data) {
    (void)f_user_data;

    if (f_audio_size > 0U) {
        atomic_store(&g_audio_callback_received_flag, 1);
        atomic_fetch_add(&g_audio_bytes_received_count, (long)f_audio_size);
        if (g_tts_output_file != NULL && f_audio_data != NULL) {
            size_t t_written_size = fwrite(f_audio_data, 1U, f_audio_size, g_tts_output_file);
            if (t_written_size != f_audio_size) {
                atomic_store(&g_tts_output_write_failed_flag, 1);
                fprintf(stderr, "[device_voice_example] write tts output file failed\n");
            }
        }
        if (atomic_load(&g_tts_stream_playback_enabled_flag) != 0
                && writeAplayStream(f_audio_data, f_audio_size) != 0) {
            fprintf(
                    stderr,
                    "[device_voice_example] aplay stream write failed: %s\n",
                    g_aplay_stream_error_message[0] != '\0' ? g_aplay_stream_error_message : "unknown"
            );
        }
    }
    if (f_audio_size > 0U && atomic_load(&g_audio_bytes_received_count) == (long)f_audio_size) {
        printf("[device_voice_example] tts audio streaming started: first_chunk_bytes=%zu\n", f_audio_size);
    }
}

static void onError(int f_error_code, const char *f_error_message, void *f_user_data) {
    (void)f_user_data;
    printf(
            "[device_voice_example] error(%d): %s\n",
            f_error_code,
            f_error_message == NULL ? "" : f_error_message
    );
}

int main(void) {
    const char *t_ws_url = NULL;
    const char *t_api_base_url = NULL;
    const char *t_device_id = NULL;
    const char *t_device_secret = NULL;
    const char *t_client_id = NULL;
    const char *t_ca_bundle_path = NULL;
    const char *t_pcm_file_path = NULL;
    const char *t_alsa_capture_device = NULL;
    const char *t_alsa_playback_device = NULL;
    unsigned char *t_pcm_bytes = NULL;
    size_t t_pcm_size = 0U;
    int t_exit_code = 1;
    int t_alsa_capture_enabled_flag = 0;
    int t_disable_playback_flag = 0;
    int t_record_seconds = 0;
    int t_record_until_endpoint_flag = 1;
    int t_max_record_seconds = DEFAULT_MAX_RECORD_SECONDS;
    int t_tts_sample_rate = PCM_SAMPLE_RATE;
    char t_capture_pcm_path[PATH_MAX] = {0};
    AgentClient *t_client = NULL;
    AgentDeviceActivationStatus t_status = {0};
    AgentDeviceTokenResult t_token = {0};
    const char *t_record_seconds_text = NULL;

    setvbuf(stdout, NULL, _IONBF, 0);

    t_ws_url = readRequiredEnv("AGENT_WS_URL");
    t_device_id = readRequiredEnv("AGENT_DEVICE_ID");
    t_device_secret = readRequiredEnv("AGENT_DEVICE_SECRET");
    if (t_ws_url == NULL || t_device_id == NULL || t_device_secret == NULL) {
        return 1;
    }
    t_api_base_url = readOptionalEnv("AGENT_API_BASE_URL", "");
    t_client_id = readOptionalEnv("AGENT_CLIENT_ID", "device-voice-example");
    t_ca_bundle_path = readOptionalEnv("AGENT_CA_BUNDLE", "");
    t_pcm_file_path = readOptionalEnv("AGENT_PCM_FILE", "");
    t_alsa_capture_device = readOptionalEnv("AGENT_ALSA_CAPTURE_DEVICE", "");
    t_alsa_playback_device = readOptionalEnv("AGENT_ALSA_PLAYBACK_DEVICE", "");
    t_alsa_capture_enabled_flag = readOptionalBool("AGENT_ALSA_CAPTURE");
    t_disable_playback_flag = readOptionalBool("AGENT_DISABLE_PLAYBACK");
    t_record_seconds_text = getenv("AGENT_RECORD_SECONDS");
    if (t_record_seconds_text != NULL && t_record_seconds_text[0] != '\0') {
        t_record_seconds = atoi(t_record_seconds_text);
    }
    t_record_until_endpoint_flag = t_record_seconds <= 0 ? 1 : 0;
    t_max_record_seconds = readOptionalInt("AGENT_MAX_RECORD_SECONDS", DEFAULT_MAX_RECORD_SECONDS);
    t_tts_sample_rate = readOptionalInt("AGENT_TTS_SAMPLE_RATE", PCM_SAMPLE_RATE);

    if (t_ca_bundle_path[0] != '\0') {
        printf("[device_voice_example] use ca bundle: %s\n", t_ca_bundle_path);
    }
    resetAplayStreamState(t_tts_sample_rate, t_alsa_playback_device);

    AgentConfig t_config = {
            .ws_url = t_ws_url,
            .device_id = t_device_id,
            .client_id = t_client_id,
            .authorization = "",
            .agent_id = "",
            .user_id = "",
            .tts_tone_id = "",
            .audio_format = "pcm",
            .sample_rate = PCM_SAMPLE_RATE,
            .channels = PCM_CHANNELS,
            .frame_duration_ms = PCM_FRAME_DURATION_MS,
            .feature_iot = 0,
            .feature_speaker = 1,
            .feature_mcp = 0
    };

    t_client = agentCreateClient(&t_config);
    if (t_client == NULL) {
        fprintf(stderr, "[device_voice_example] create client failed\n");
        return 1;
    }

    agentSetStatusCallback(t_client, onStatus, NULL);
    agentSetMessageCallback(t_client, onMessage, NULL);
    agentSetAudioCallback(t_client, onAudio, NULL);
    agentSetErrorCallback(t_client, onError, NULL);

    if (agentSetDeviceSecret(t_client, t_device_secret) != AGENT_OK) {
        fprintf(stderr, "[device_voice_example] agentSetDeviceSecret failed\n");
        goto cleanup;
    }
    if (t_api_base_url[0] != '\0' && agentSetApiBaseUrl(t_client, t_api_base_url) != AGENT_OK) {
        fprintf(stderr, "[device_voice_example] agentSetApiBaseUrl failed\n");
        goto cleanup;
    }

    if (agentCheckDeviceActivation(t_client, &t_status) != AGENT_OK) {
        fprintf(stderr, "[device_voice_example] agentCheckDeviceActivation failed\n");
        goto cleanup;
    }
    printActivationStatus(&t_status);
    if (!t_status.m_activated_flag || !t_status.m_bound_flag) {
        if (printPairingCodeIfNeeded(t_client, &t_status) == AGENT_OK) {
            t_exit_code = 2;
        }
        goto cleanup;
    }

    if (ensureAuthorizedConnectionWithRetry(t_client, &t_status, &t_token) != AGENT_OK) {
        fprintf(stderr, "[device_voice_example] agentEnsureAuthorizedConnection failed\n");
        goto cleanup;
    }
    printf(
            "[device_voice_example] authorized: token_type=%s expires_in=%ld device_id=%s\n",
            t_token.m_token_type,
            t_token.m_expires_in,
            t_token.m_device_id
    );

    if (t_disable_playback_flag == 0 && prepareTtsOutputFile() != 0) {
        fprintf(
                stderr,
                "[device_voice_example] prepare tts output file failed, continue with stream playback only\n"
        );
    }
    atomic_store(&g_tts_stream_playback_enabled_flag, t_disable_playback_flag == 0 ? 1 : 0);
    atomic_store(&g_session_ready_received_flag, 0);
    atomic_store(&g_audio_stream_started_flag, 0);
    atomic_store(&g_audio_stream_endpoint_detected_flag, 0);
    atomic_store(&g_asr_final_received_flag, 0);
    atomic_store(&g_tts_stream_finished_flag, 0);
    atomic_store(&g_assistant_response_received_flag, 0);
    atomic_store(&g_audio_callback_received_flag, 0);
    atomic_store(&g_tts_output_write_failed_flag, 0);
    atomic_store(&g_audio_bytes_received_count, 0);

    if (t_pcm_file_path[0] != '\0') {
        if (loadPcmFile(t_pcm_file_path, &t_pcm_bytes, &t_pcm_size) != 0) {
            fprintf(stderr, "[device_voice_example] load pcm file failed: %s\n", t_pcm_file_path);
            goto cleanup;
        }
        printf("[device_voice_example] send pcm file: path=%s bytes=%zu\n", t_pcm_file_path, t_pcm_size);
        if (sendPcmAudioStream(t_client, t_pcm_bytes, t_pcm_size) != AGENT_OK) {
            fprintf(stderr, "[device_voice_example] sendPcmAudioStream failed\n");
            goto cleanup;
        }
    } else if (t_alsa_capture_enabled_flag != 0) {
        if (
                sendLiveArecordAudioStream(
                        t_client,
                        t_alsa_capture_device,
                        t_record_until_endpoint_flag,
                        t_record_seconds,
                        t_max_record_seconds
                ) != AGENT_OK
        ) {
            fprintf(stderr, "[device_voice_example] sendLiveArecordAudioStream failed\n");
            goto cleanup;
        }
    } else {
        printf("[device_voice_example] AGENT_PCM_FILE not set, use built-in synthetic frames\n");
        if (sendSyntheticAudioStream(t_client) != AGENT_OK) {
            fprintf(stderr, "[device_voice_example] sendSyntheticAudioStream failed\n");
            goto cleanup;
        }
    }

    if (waitForConversationResult(t_disable_playback_flag == 0) != 0) {
        fprintf(stderr, "[device_voice_example] wait for conversation result timeout\n");
        goto cleanup;
    }

    int t_stream_finish_result = 0;
    if (t_disable_playback_flag == 0) {
        t_stream_finish_result = finishAplayStream();
        if (t_stream_finish_result != 0) {
            fprintf(
                    stderr,
                    "[device_voice_example] finishAplayStream failed: %s\n",
                    g_aplay_stream_error_message[0] != '\0' ? g_aplay_stream_error_message : "unknown"
            );
        }
    }
    closeTtsOutputFile();
    if (atomic_load(&g_tts_output_write_failed_flag) != 0) {
        fprintf(stderr, "[device_voice_example] tts output file write failed during callback\n");
        goto cleanup;
    }
    if (
            t_disable_playback_flag == 0
            && (
                    atomic_load(&g_tts_stream_playback_started_flag) == 0
                    || atomic_load(&g_tts_stream_playback_failed_flag) != 0
                    || t_stream_finish_result != 0
            )
            && g_tts_output_path[0] != '\0'
            && atomic_load(&g_audio_bytes_received_count) > 0
            && playPcmWithAplay(g_tts_output_path, t_tts_sample_rate, t_alsa_playback_device) != 0
    ) {
        fprintf(stderr, "[device_voice_example] playPcmWithAplay failed\n");
        goto cleanup;
    }

    printf(
            "[device_voice_example] summary: session_ready=%d asr_final=%d tts_stream_end=%d assistant_response=%d audio_bytes=%ld capture_path=%s tts_path=%s\n",
            atomic_load(&g_session_ready_received_flag),
            atomic_load(&g_asr_final_received_flag),
            atomic_load(&g_tts_stream_finished_flag),
            atomic_load(&g_assistant_response_received_flag),
            atomic_load(&g_audio_bytes_received_count),
            t_capture_pcm_path[0] == '\0' ? "" : t_capture_pcm_path,
            g_tts_output_path[0] == '\0' ? "" : g_tts_output_path
    );
    t_exit_code = 0;

cleanup:
    abortAplayStream();
    closeTtsOutputFile();
    if (t_client != NULL) {
        (void)agentDisconnect(t_client);
        agentDestroyClient(t_client);
    }
    free(t_pcm_bytes);
    return t_exit_code;
}
