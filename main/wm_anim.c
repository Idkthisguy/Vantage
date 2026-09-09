#include "wm_anim.h"

static void anim_set_x_cb(void *var, int32_t v) {
  lv_obj_set_x((lv_obj_t *)var, (lv_coord_t)v);
}

static void anim_set_y_cb(void *var, int32_t v) {
  lv_obj_set_y((lv_obj_t *)var, (lv_coord_t)v);
}

static void anim_set_width_cb(void *var, int32_t v) {
  lv_obj_set_width((lv_obj_t *)var, (lv_coord_t)v);
}

static void anim_set_height_cb(void *var, int32_t v) {
  lv_obj_set_height((lv_obj_t *)var, (lv_coord_t)v);
}

static void anim_set_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void anim_set_radius_cb(void *var, int32_t v) {
  lv_obj_set_style_radius((lv_obj_t *)var, v, 0);
}

static void ghost_del_cb(lv_anim_t *a) {
  lv_obj_t *ghost = (lv_obj_t *)lv_anim_get_user_data(a);
  if (ghost && lv_obj_is_valid(ghost)) {
    lv_obj_del(ghost);
  }
}

void wm_animate_launch(lv_obj_t *src, lv_obj_t *target) {
  if (!src || !target) {
    return;
  }

  lv_area_t src_area;
  lv_area_t target_area;
  lv_obj_get_coords(src, &src_area);
  lv_obj_get_coords(target, &target_area);

  lv_coord_t src_w = lv_area_get_width(&src_area);
  lv_coord_t src_h = lv_area_get_height(&src_area);
  lv_coord_t target_w = lv_area_get_width(&target_area);
  lv_coord_t target_h = lv_area_get_height(&target_area);

  /* Create a ghost clone of the source on the top layer. */
  lv_obj_t *ghost = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(ghost);
  lv_obj_set_pos(ghost, src_area.x1, src_area.y1);
  lv_obj_set_size(ghost, src_w, src_h);

  lv_color_t bg_color = lv_obj_get_style_bg_color(src, LV_PART_MAIN);
  int32_t radius = lv_obj_get_style_radius(src, LV_PART_MAIN);

  lv_obj_set_style_bg_color(ghost, bg_color, 0);
  lv_obj_set_style_radius(ghost, radius, 0);
  lv_obj_set_style_bg_opa(ghost, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ghost, 0, 0);

  /* Do not clone the icon glyph: LVGL resizes the card but not a font glyph,
   * which made the previous transition look like a floating, detached icon. */

  /* Start the target transparent; it will fade in behind the ghost. */
  lv_obj_set_style_opa(target, LV_OPA_TRANSP, 0);

  lv_anim_t a;

  /* Position */
  lv_anim_init(&a);
  lv_anim_set_var(&a, ghost);
  lv_anim_set_values(&a, src_area.x1, target_area.x1);
  lv_anim_set_duration(&a, 260);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, anim_set_x_cb);
  lv_anim_start(&a);

  lv_anim_init(&a);
  lv_anim_set_var(&a, ghost);
  lv_anim_set_values(&a, src_area.y1, target_area.y1);
  lv_anim_set_duration(&a, 260);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, anim_set_y_cb);
  lv_anim_start(&a);

  /* Size */
  lv_anim_init(&a);
  lv_anim_set_var(&a, ghost);
  lv_anim_set_values(&a, src_w, target_w);
  lv_anim_set_duration(&a, 260);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, anim_set_width_cb);
  lv_anim_start(&a);

  lv_anim_init(&a);
  lv_anim_set_var(&a, ghost);
  lv_anim_set_values(&a, src_h, target_h);
  lv_anim_set_duration(&a, 260);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, anim_set_height_cb);
  lv_anim_start(&a);

  /* Square the expanding card as it reaches the full-screen app surface. */
  lv_anim_init(&a);
  lv_anim_set_var(&a, ghost);
  lv_anim_set_values(&a, radius, 0);
  lv_anim_set_duration(&a, 260);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, anim_set_radius_cb);
  lv_anim_start(&a);

  /* Fade out and delete the ghost at the end. */
  lv_anim_init(&a);
  lv_anim_set_var(&a, ghost);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_duration(&a, 145);
  lv_anim_set_delay(&a, 115);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_exec_cb(&a, anim_set_opa_cb);
  lv_anim_set_user_data(&a, ghost);
  lv_anim_set_completed_cb(&a, ghost_del_cb);
  lv_anim_start(&a);

  /* The early overlap is the whole visual trick. It looks deceptively simple;
   * please profile before "cleaning it up" and bringing back the hard cut. */
  lv_anim_init(&a);
  lv_anim_set_var(&a, target);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, 190);
  lv_anim_set_delay(&a, 55);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, anim_set_opa_cb);
  lv_anim_start(&a);
}
