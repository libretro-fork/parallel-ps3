#ifndef LIBRETRO_PATCH_OPTIONS_H__
#define LIBRETRO_PATCH_OPTIONS_H__

/* RPCS3's game patches as core options, the Patch Manager category: which
 * of a game's patches are listed, their option definitions, which value
 * choices show, and what patch_config.yml gets from them. C89; the files
 * are read by libretro_patch_manager.cpp. */

#include <stddef.h>

#include <libretro.h>

#include "../../Utilities/patch_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An option's value, as the frontend has it ("enabled", a label), or NULL */
typedef const char *(*patch_options_get_t)(void *user, const char *key);

/* Messages, as patch_db's */
void patch_options_set_log(patch_db_log_t log, void *user);

/* The options for the game @serial from @db (kept, by pointer, until the
 * next call); NULL, or an empty serial, for none. Also turns every text
 * patch off. */
void patch_options_collect(const patch_db_t *db, const char *serial);

/* Whether the game has any patch, and so the category */
int patch_options_any(void);

/* The definitions, @count of them, without the terminating one: valid
 * until the next collect */
const struct retro_core_option_v2_definition *patch_options_definitions(size_t *count);

/* Turning on a patch of a group turns the group's others off; a patch's
 * value choices show only while it is on */
void patch_options_update_display(retro_environment_t cb, patch_options_get_t get, void *user);

/* Turns on the text patches whose options are on, and returns what
 * patch_config.yml is to turn on: @count choices, valid until the next
 * call */
const patch_config_choice_t *patch_options_choices(patch_options_get_t get, void *user, size_t *count);

/* The frame rate the patches that are on ask for, for VBlank Frequency
 * "Auto": of their values named for it ("FPS", "Frame Rate"), the highest
 * number a picked label starts its digits with ("120 FPS"), or the value
 * itself when the label has none. 0 when no patch asks for one. */
unsigned patch_options_frame_rate(patch_options_get_t get, void *user);

#ifdef __cplusplus
}
#endif

#endif
