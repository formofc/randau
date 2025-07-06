#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#define RECORD_FRAME_SIZE       1024
#define OSCILLATORS_MAX_COUNT   128
#define DEFAULT_SAMPLE_RATE     44100
#define DEFAULT_DURATION_SEC    7.0
#define DEFAULT_OSCILLATORS     7
#define DEFAULT_MASTER_VOLUME   0.15
#define DEFAULT_MIN_FREQ        110.0
#define DEFAULT_MAX_FREQ        440.0
#define DEFAULT_MIN_BPS         0.1
#define DEFAULT_MAX_BPS         10.0
#define DEFAULT_MIN_AMP         0.1
#define DEFAULT_MAX_AMP         1.0

#define RANDAU_MIN(x, y) ((x) > (y) ? (y) : (x)) 

/* wierd ansi stuff */
typedef char ra_bool;

typedef enum {
   OSCILLATOR_TYPE_FLAT,
   OSCILLATOR_TYPE_SAWTOOTH,
   OSCILLATOR_TYPE_NOISE,
   OSCILLATOR_TYPE_PULSE,
   OSCILLATOR_TYPE_BEAT,
   OSCILLATOR_TYPE_WAVE,
   OSCILLATOR_TYPE_MAX_ENUM,

} oscillator_type_t;

typedef struct {
    float phase;
    float freq;
    float amp;
    float bps;
    oscillator_type_t type;

} oscillator_t;

typedef enum {
    SOUND_ACTION_ONLY_PLAY,
    SOUND_ACTION_PLAY_AND_RECORD,
    SOUND_ACTION_ONLY_RECORD,

} sound_action_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t oscillators;
    uint32_t loop_count;
    float master_volume;
    float duration_sec;
    float min_freq;
    float max_freq;
    float min_bps;
    float max_bps;
    float min_amp;
    float max_amp;
    const char* save_path;
    sound_action_t sound_action;
    ra_bool limited;

} config_t;

typedef struct {
    uint32_t sample_rate;
    uint32_t loop_count;
    oscillator_t* oscillators;
    size_t oscillators_count;
    float duration_sec;
    float master_volume;
    float global_time;
    float min_freq;
    float max_freq;
    float min_bps;
    float max_bps;
    float min_amp;
    float max_amp;
    ma_encoder* encoder;
    sound_action_t sound_action;
    ra_bool limited;

} context_t;

/*
 * return [0, max)
 */
uint32_t random_uint(uint32_t max) {
    return ((uint32_t)rand() % max);
}

/*
 * return [min, max]
 */
float random_float_range(float min, float max) {
    if (min >= max) return min;

    return min + ((float)rand() / (float)RAND_MAX) * (max - min); 
}

void randomize_oscillators(context_t* ctx) {
    size_t i;
    oscillator_t* osc = NULL;

    if (!ctx) return;

    for (i = 0; i < ctx->oscillators_count; ++i) {
        osc = &ctx->oscillators[i];
        osc->type = (oscillator_type_t)(rand() % OSCILLATOR_TYPE_MAX_ENUM);
        osc->freq = random_float_range(ctx->min_freq, ctx->max_freq);
        osc->bps = random_float_range(ctx->min_bps, ctx->max_bps);
        osc->amp = random_float_range(ctx->min_amp, ctx->max_amp);
        osc->phase = 0.0f;
    }
}

#define BEAT_AMP_SCALE         7.5f 
#define BEAT_ATTACK_SHARPNESS  8.0f
#define BEAT_DECAY_RATE        5.0f
#define BEAT_SUSTAIN_LEVEL     0.3f
#define BEAT_RELEASE_TIME      0.4f

float generate_beat(context_t* ctx, float base, float bps) {
    float phase, env = 0.0, wave;

    if (!ctx) return 0.0f;
    
    phase = fmod(ctx->global_time * bps, 1.0f);
    
    if (phase < 0.1f) {
        env = 1.0f - exp(-phase * BEAT_ATTACK_SHARPNESS);
    } 
    else if (phase < BEAT_RELEASE_TIME) {
        env = BEAT_SUSTAIN_LEVEL + 
              (1.0f - BEAT_SUSTAIN_LEVEL) * 
              exp(-(phase - 0.1f) * BEAT_DECAY_RATE);
    }
    else {
        env = BEAT_SUSTAIN_LEVEL * 
              (1.0f - (phase - BEAT_RELEASE_TIME) / (1.0f - BEAT_RELEASE_TIME));
    }

    wave = 2.0f * exp(sin(2.0f * MA_PI * phase) - 1.0f) - 1.0f;
    
    return base * BEAT_AMP_SCALE * wave * env;
}

float generate_wave(context_t* ctx, float phase, float bps, int type) {
    float base;

    if (!ctx) return 0.0f;

    if (type != OSCILLATOR_TYPE_SAWTOOTH && type != OSCILLATOR_TYPE_NOISE) {
        base = sin(phase);
    }

    switch (type) {
        case OSCILLATOR_TYPE_FLAT: return base;
        case OSCILLATOR_TYPE_SAWTOOTH: return 2.0f * (phase / (2*MA_PI)) - 1.0f;
        case OSCILLATOR_TYPE_NOISE: return random_float_range(-1.0f, 1.0f);
        case OSCILLATOR_TYPE_BEAT: {
            return generate_beat(ctx, base, bps);
        }
        case OSCILLATOR_TYPE_WAVE: {
            return base * sin(ctx->global_time * MA_PI * bps);
        }
        case OSCILLATOR_TYPE_PULSE: {
            return base * exp(sin(ctx->global_time * MA_PI * bps)) * exp(-1.0) * 2.0 - 1.0;
        }

        default: return 0.0f;
    }
}
/*Returns 1 if the duration has been reset (new cycle started), 0 otherwise.*/
ra_bool update_global_time(context_t* ctx) {
    if (!ctx) return 0;

    ctx->global_time += 1.0f / ctx->sample_rate;

    if (ctx->global_time >= ctx->duration_sec) { /* Randomize all */
        ctx->global_time = 0.0f;
        randomize_oscillators(ctx);
        return 1;
    }

    return 0;
}

float generate_next_sample(context_t* ctx) {
    size_t j;
    oscillator_t* osc = NULL;
    float sample = 0.0f, envelope;

    if (!ctx) return 0.0;

    for (j = 0; j < ctx->oscillators_count; ++j) {
        osc = &ctx->oscillators[j];
        osc->phase += 2.0f * MA_PI * osc->freq / ctx->sample_rate;
        
        if (osc->phase >= 2.0f * MA_PI) osc->phase -= 2.0f * MA_PI;
        
        sample += ctx->master_volume * osc->amp * generate_wave(ctx, osc->phase, osc->bps, osc->type);
    }

    /* Simple envelope to avoid clicks */
    envelope = (ctx->global_time < 0.1f) ? ctx->global_time * 10.0f : 1.0f;
    envelope *= (ctx->duration_sec - ctx->global_time < 0.1f) ? (ctx->duration_sec - ctx->global_time) * 10.0f : 1.0f;

    return sample * envelope * 0.15f;
}

void data_callback(ma_device* pDevice, void* ma_output, const void* pInput, ma_uint32 frame_count) {
    ma_uint32 i; /* size_t - BLOAT. i will never > ma_uint32 cuz frame_count is u32 */
    ma_uint64 dummy_written;
    float* output = (float*)ma_output, sample;
    context_t* ctx = (context_t*)pDevice->pUserData;
    
    if (!ctx) return;

    for (i = 0; i < frame_count; ++i) {
        sample = generate_next_sample(ctx);
        output[i] = sample;
        (void)update_global_time(ctx);
    }

    if (ctx->sound_action != SOUND_ACTION_ONLY_PLAY) {
        if (ma_encoder_write_pcm_frames(ctx->encoder, output, i, &dummy_written) != MA_SUCCESS) {
            fprintf(stderr, "Failed to write pcm frame to output");
            
        }
    }
    (void)pInput;
    (void)dummy_written;
}
void print_help(char* selfpath) {
    printf("Usage: %s [options]\n", selfpath);
    printf(
        "Options:\n"
        "  -h, --help                   Show this help message\n"
        "  -r, --sample-rate R          Set sample rate (default: %u)\n"
        "  -d, --duration D             Set duration in seconds (default: %f)\n"
        "  -c, --oscillators C          Set number of oscillators (default: %u, maximum: %d)\n"
        "  -C, --loop-count L           Set loop count for recording (only with -S)\n"
        "  -s, --save FILE              Save while playing (FILE is output .wav)\n"
        "  -S, --save-only FILE         Save without playing (FILE is output .wav)\n"
        "  -v, --volume V               Set master volume (default: %f)\n"
        "  -f, --freq-range N X         Set frequency range (default: [%f, %f])\n"
        "  -a, --amplitude-range N X    Set amplitude range (default: [%f, %f])\n"
        "  -b, --bps-range N X          Set beat per second range (default: [%f, %f])\n",

        DEFAULT_SAMPLE_RATE,
        DEFAULT_DURATION_SEC,
        DEFAULT_OSCILLATORS, OSCILLATORS_MAX_COUNT,
        DEFAULT_MASTER_VOLUME,
        DEFAULT_MIN_FREQ, DEFAULT_MAX_FREQ,
        DEFAULT_MIN_AMP, DEFAULT_MAX_AMP,
        DEFAULT_MIN_BPS, DEFAULT_MAX_BPS
    );
}
ra_bool error_msg(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    return 0;
}
/*
 * Huge ass function
 */
ra_bool parse_config(config_t* cfg, int argc, char** argv) {
    char* selfpath;
    float f_dummy;
    uint32_t u_dummy;

    if (!cfg || !argv) return 0;

    selfpath = *argv;
    ++argv;
    *cfg = (config_t) {
        .master_volume = DEFAULT_MASTER_VOLUME,
        .min_freq = DEFAULT_MIN_FREQ,
        .max_freq = DEFAULT_MAX_FREQ,
        .min_bps = DEFAULT_MIN_BPS,
        .max_bps = DEFAULT_MAX_BPS,
        .min_amp = DEFAULT_MIN_AMP,
        .max_amp = DEFAULT_MAX_AMP,
        .sample_rate = DEFAULT_SAMPLE_RATE,
        .duration_sec = DEFAULT_DURATION_SEC,
        .oscillators = DEFAULT_OSCILLATORS,
        .save_path = NULL,
        .sound_action = SOUND_ACTION_ONLY_PLAY,
        .loop_count = 1,
        .limited = 0,
    };

    if (argc <= 1) return 1;
    
    /*
     * strcmp(n, m) == 0 || strcmp(x, y) == 0
     * exacly =
     * strcmp(n, m) * strcmp(x, y) == 0
     *
     * but require 2 compares even if first passed
     */

    while (*argv) {
        if (strcmp(*argv, "-h") == 0 || strcmp(*argv, "--help") == 0) {
            print_help(selfpath);
            return 0;

        } else if (strcmp(*argv, "-S") == 0 || strcmp(*argv, "--save-only") == 0) {
            if (*(argv + 1) == NULL) return error_msg("Argument list eof on `-S`\n");
            ++argv;
            
            if (strstr(*argv, "..") != 0) return error_msg("Error: Path '%s' contains '..' (potential directory traversal).\nUse absolute paths or filenames without '..'.\n", *argv);
            /* anti autistic barrier up here */

            cfg->save_path = *argv;
            cfg->sound_action = SOUND_ACTION_ONLY_RECORD;
            cfg->limited = 1;

        } else if (strcmp(*argv, "-s") == 0 || strcmp(*argv, "--save") == 0) {
            if (*(argv + 1) == NULL) return error_msg("Argument list eof on `-s`\n");
            ++argv;

            if (strstr(*argv, "..") != 0) return error_msg("Error: Path '%s' contains '..' (potential directory traversal).\nUse absolute paths or filenames without '..'.\n", *argv);
            /* anti autistic barrier up here */

            cfg->save_path = *argv;
            cfg->sound_action = SOUND_ACTION_PLAY_AND_RECORD;

        } else if (strcmp(*argv, "-r") == 0 || strcmp(*argv, "--sample-rate") == 0) {
            if (*(argv + 1) == NULL) return error_msg("Argument list eof on `-r`\n");
            
            ++argv;
            if (sscanf(*argv, "%u", &u_dummy) != 1) return error_msg("Invalid option value. Expect a positive integer `%s`\n", *argv);
            if (u_dummy == 0) return error_msg("Invalid sample rate\n");
            cfg->sample_rate = u_dummy;

        } else if (strcmp(*argv, "-C") == 0 || strcmp(*argv, "--loop-count") == 0) {
            if (*(argv + 1) == NULL) return error_msg("Argument list eof on `-C`\n");

            ++argv;
            if (sscanf(*argv, "%u", &u_dummy) != 1) return error_msg("Invalid option value. Expect a positive integer `%s`\n", *argv);
            if (u_dummy == 0) return error_msg("Invalid loop count count\n");

            cfg->limited = 1;
            cfg->loop_count = u_dummy;

        } else if (strcmp(*argv, "-c") == 0 || strcmp(*argv, "--osclillators") == 0) {
            if (*(argv + 1) == NULL) return error_msg("Argument list eof on `-c`\n");

            ++argv;
            if (sscanf(*argv, "%u", &u_dummy) != 1) return error_msg("Invalid option value. Expect a positive integer `%s`\n", *argv);
            if (u_dummy == 0 || u_dummy > OSCILLATORS_MAX_COUNT) return error_msg("Invalid oscillators count\n");

            cfg->oscillators = u_dummy;

        } else if (strcmp(*argv, "-d") == 0 || strcmp(*argv, "--duration") == 0) {
            if (*(argv + 1) == NULL) return error_msg("Argument list eof on `-d`");

            ++argv;
            if (sscanf(*argv, "%f", &f_dummy) != 1) return error_msg("Invalid option value. Expect a number `%s`\n", *argv);
            if (f_dummy <= 0) return error_msg("Invalid loop duration\n");

            cfg->duration_sec = f_dummy;

        } else if (strcmp(*argv, "-f") == 0 || strcmp(*argv, "--freq-range") == 0) {
            if (!*(argv + 1) || !*(argv + 2)) return error_msg("Need min and max values");
            if (sscanf(*++argv, "%f", &cfg->min_freq) != 1 ||
                sscanf(*++argv, "%f", &cfg->max_freq) != 1 ||
                cfg->min_freq > cfg->max_freq)
                return error_msg("Invalid frequency range");
        }
        else if (strcmp(*argv, "-b") == 0 || strcmp(*argv, "--bps-range") == 0) {
            if (!*(argv + 1) || !*(argv + 2)) return error_msg("Need min and max values");
            if (sscanf(*++argv, "%f", &cfg->min_bps) != 1 ||
                sscanf(*++argv, "%f", &cfg->max_bps) != 1 ||
                cfg->min_bps > cfg->max_bps)
                return error_msg("Invalid BPS range");
        }
        else if (strcmp(*argv, "-a") == 0 || strcmp(*argv, "--amplitude-range") == 0) {
            if (!*(argv + 1) || !*(argv + 2)) return error_msg("Need min and max values");
            if (sscanf(*++argv, "%f", &cfg->min_amp) != 1 ||
                sscanf(*++argv, "%f", &cfg->max_amp) != 1 ||
                cfg->min_amp > cfg->max_amp)
                return error_msg("Invalid amplitude range");
        }
        else if (strcmp(*argv, "-v") == 0 || strcmp(*argv, "--volume") == 0) {
            if (!*(argv + 1)) return error_msg("Argument list eof on `-v`");
            if (sscanf(*++argv, "%f", &f_dummy) != 1 || f_dummy < 0 || f_dummy > 1.0f) return error_msg("Volume must be 0..1");
            
            cfg->master_volume = f_dummy;
        } else {
            return error_msg("Unknown argument `%s`\n", *argv);

        }
        ++argv;
    }

    if (cfg->sound_action == SOUND_ACTION_ONLY_PLAY && cfg->save_path) return error_msg("Something wrong: only play mode but no save path\n");
    if (cfg->limited && cfg->sound_action != SOUND_ACTION_ONLY_RECORD) return error_msg("Invalid usage. Trying to set limit in non record-only mode\n");

    return 1;
}

ra_bool init_encoder(ma_encoder* encoder, context_t* ctx, const char* filepath) {
    ma_encoder_config encoder_config = {0};
    
    if (!encoder || !ctx || !filepath) return 0;

    encoder_config = ma_encoder_config_init(
        ma_encoding_format_wav,
        ma_format_f32,
        1,
        ctx->sample_rate
    );
    
    if (ma_encoder_init_file(filepath, &encoder_config, ctx->encoder) != MA_SUCCESS) {
        return error_msg("Failed to initialize encoder\n");
    }
    
    return 1;
}

void record_single_loop(context_t* ctx) {
    ma_uint64 dummy_written;
    float output[RECORD_FRAME_SIZE], sample;
    size_t sample_rest, current_samples, i;
    
    if (!ctx) return;
    sample_rest = (size_t)(ctx->duration_sec * 1000.0) * ctx->sample_rate / 1000;

    while (sample_rest) {
        current_samples = RANDAU_MIN(RECORD_FRAME_SIZE, sample_rest);
        for (i = 0; i < current_samples; ++i) {
            sample = generate_next_sample(ctx);
            output[i] = sample;
            if (update_global_time(ctx)) break;
        }
        sample_rest -= current_samples;
        ma_encoder_write_pcm_frames(ctx->encoder, output, current_samples, &dummy_written);
    }
    (void)dummy_written;
}

ra_bool init_device(ma_device* device, config_t* cfg, context_t* ctx) {
    ma_device_config ma_config = {0};

    if (!device || !cfg || !ctx) return 0;

    ma_config = ma_device_config_init(ma_device_type_playback);
    ma_config.playback.format = ma_format_f32;
    ma_config.playback.channels = 1;
    ma_config.sampleRate = cfg->sample_rate;
    ma_config.dataCallback = data_callback;
    ma_config.pUserData = ctx;

    if (ma_device_init(NULL, &ma_config, device) != MA_SUCCESS) {
        printf("Failed to initialize audio device\n");
        return 0;
    }

    return 1;
}

void record_all_loops(context_t* ctx) {
    size_t i;

    if (!ctx) return;

    for (i = 0; i < ctx->loop_count; ++i) {
        record_single_loop(ctx);
    }
}

int main(int argc, char** argv) {
    ma_device device;
    config_t cfg;
    context_t ctx = {0};
    oscillator_t oscillators[OSCILLATORS_MAX_COUNT];
    ma_encoder encoder;
    ra_bool encoder_inited = 0, device_inited = 0, success = 0;

    if (!parse_config(&cfg, argc, argv)) goto cleanup;

    ctx = (context_t) {
        .oscillators_count = cfg.oscillators,
        .loop_count = cfg.loop_count,
        .duration_sec = cfg.duration_sec,
        .oscillators = oscillators,
        .sample_rate = cfg.sample_rate,
        .sound_action = cfg.sound_action,
        .encoder = &encoder,
        .limited = cfg.limited,
        .master_volume = cfg.master_volume,
        .min_freq = cfg.min_freq,
        .max_freq = cfg.max_freq,
        .min_bps = cfg.min_bps,
        .max_bps = cfg.max_bps,
        .min_amp = cfg.min_amp,
        .max_amp = cfg.max_amp,
    };

    if (!ctx.oscillators) {
        fprintf(stderr, "Failed to allocate memory for oscillators. Buy more RAM\n");
        goto cleanup;
    }
    
    srand(time(NULL));
    randomize_oscillators(&ctx);

    if (cfg.save_path) {
        if (!init_encoder(&encoder, &ctx, cfg.save_path)) goto cleanup;

        encoder_inited = 1;
    }


    if (cfg.save_path && ctx.sound_action == SOUND_ACTION_ONLY_RECORD) {
        record_all_loops(&ctx);

    } else {
        if (!init_device(&device, &cfg, &ctx)) goto cleanup;
        device_inited = 1;

        ma_device_start(&device);
        if (ctx.sound_action != SOUND_ACTION_ONLY_PLAY) {
            printf("Recording to `%s`... Press Enter to stop\n", cfg.save_path);
        } else {
            printf("Playing... Press Enter to stop\n");
        }

        getchar();

    }

    success = 1;
cleanup:
    if (encoder_inited) ma_encoder_uninit(&encoder);
    if (device_inited) ma_device_uninit(&device);

    if (!success) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
