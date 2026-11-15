#pragma once

struct CodmFrame;

void jnmaini_style_init_once();
void jnmaini_draw_frame(bool menuOpen, bool& isToggled, const CodmFrame* frame);
void jnmaini_draw_external_panel();