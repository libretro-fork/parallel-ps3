/* Runs one shader stage from two SPIR-V modules on a Vulkan driver and
 * compares what the two make (shader_runner.c). Used by
 * test_shader_corpus.cpp. */

#ifndef SHADER_RUNNER_H
#define SHADER_RUNNER_H

#include <stddef.h>
#include <stdint.h>

#include <volk.h>

#ifdef __cplusplus
extern "C" {
#endif

enum shader_run_verdict
{
   SHADER_RUN_SAME = 0,
   SHADER_RUN_DIFFERS,
   SHADER_RUN_NOT_RUN
};

/* Compiles the vertex stage a fragment stage is drawn with. On success
 * *words is a malloc'd module of *count words. */
typedef int (*shader_run_compile_t)(void *ud, const char *glsl, size_t len,
      uint32_t **words, size_t *count);

/* Makes the runner's own device on gpu, with transform feedback. Returns 0
 * when the driver cannot do it; nothing is run then. */
int shader_run_start(VkPhysicalDevice gpu, uint32_t queue_family);

/* Each runs the stage from module a and from module b over the same data,
 * filled three ways, and compares the results byte for byte. a is the one
 * the interface is read from. why receives a line on anything but SAME. */
enum shader_run_verdict shader_run_fragment(const uint32_t *a, size_t a_len,
      const uint32_t *b, size_t b_len, shader_run_compile_t compile, void *ud,
      char *why, size_t why_cap);
enum shader_run_verdict shader_run_vertex(const uint32_t *a, size_t a_len,
      const uint32_t *b, size_t b_len, char *why, size_t why_cap);
enum shader_run_verdict shader_run_compute(const uint32_t *a, size_t a_len,
      const uint32_t *b, size_t b_len, char *why, size_t why_cap);

#ifdef __cplusplus
}
#endif

#endif
