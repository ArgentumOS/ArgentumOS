/* argentum/theme.cpp — the Theme object (S1.3).
 *
 * S1.3 (docs/design/argentum-milestone-split.md): theme .conf loader.
 * The active theme is named by the system.theme config domain ("active"
 * key, default "Argentum"); the theme file itself lives at
 * /Shared/Themes/<name>.conf as a plain .conf data file — NOT a config
 * domain (the Configuration/ scope dirs only hold domain files), so it
 * is read with libconfig's raw-file read (config_read_file) added in
 * S1.3.
 *
 * Colour model (plan §3, decided): ONE accent in, coherent states out.
 * The theme file stores the accent + the chrome/page/text palette; the
 * per-state colours below are DERIVED from the accent by this colour
 * module (lighten/darken/desaturate RGB blends). A theme may pin any
 * derived colour by adding a key in its `derived` section:
 *
 *   derived = {
 *       chrome_outline = 0x...
 *       hover_fill = 0x...
 *       armed_fill = 0x...
 *       disabled_fill = 0x...
 *   }
 *
 * (read when present, else computed). Fallbacks when the domain name
 * is unknown or the file is missing keep the shipped Argentum look so
 * a desktop never fails to construct a Theme.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <libconfig.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* the libconfig domain naming the active theme (S1.3 decision: new
 * system.theme domain; file userland/configuration/system.theme.conf) */
#define THEME_CONF_DOMAIN "system.theme"
#define THEMES_DIR "/Shared/Themes"

/* compiled-in defaults = the shipped Argentum.conf (fallback when the
 * domain/file/keys are absent; keeps the look even without the file) */
#define DEF_NAME	"Argentum"
#define DEF_ACCENT	0x8a7fc0
#define DEF_CHROME_TOP	0xe8e8ec
#define DEF_CHROME_BOT	0xd8d8de
#define DEF_PAGE	0xf7f7f2
#define DEF_TEXT	0x202024
#define DEF_RAD_SMALL	2.0
#define DEF_RAD_BASE	3.0
#define DEF_BEVEL	1.0
#define DEF_OUTLINE	1.0
#define DEF_FONT_FAMILY	"DejaVu Sans"
#define DEF_FONT_PT	13.0

namespace argentum {

/* ---- colour derivation module (plan §3) ----------------------------
 * All helpers are straight sRGB channel math; results are rounded and
 * masked to 0xRRGGBB. */

static std::uint32_t mix(std::uint32_t a, std::uint32_t b, double t)
{
	int r = (int)std::lround(((a >> 16) & 0xff) * (1 - t) +
				 ((b >> 16) & 0xff) * t);
	int g = (int)std::lround(((a >> 8) & 0xff) * (1 - t) +
				 ((b >> 8) & 0xff) * t);
	int bl = (int)std::lround((a & 0xff) * (1 - t) + (b & 0xff) * t);
	return (std::uint32_t)((r << 16) | (g << 8) | bl) & 0xffffff;
}

static std::uint32_t lighten(std::uint32_t c, double f)
{
	return mix(c, 0xffffff, f);
}

static std::uint32_t darken(std::uint32_t c, double f)
{
	return mix(c, 0x000000, f);
}

/* relative luminance (Rec. 709) of a channel; desaturate pulls c
 * toward a grey of the same luminance. */
static double luma(std::uint32_t c)
{
	return 0.2126 * ((c >> 16) & 0xff) + 0.7152 * ((c >> 8) & 0xff) +
	       0.0722 * (c & 0xff);
}

static std::uint32_t desaturate(std::uint32_t c, double f)
{
	std::uint32_t grey = (std::uint32_t)luma(c);
	grey = (grey << 16) | (grey << 8) | grey;
	return mix(c, grey, f);
}

/* ---- raw reads ---------------------------------------------------- */

/* Read one 0xRRGGBB colour key from the theme file; 0 on error/absent.
 * config_read_file parses the file per call; theme files are small and
 * load() runs once per session, so that cost is irrelevant here. */
static std::uint32_t read_color(const char *path, const char *key)
{
	config_value_t v;

	if(config_read_file(path, key, &v)) {
		return 0;
	}
	if(v.type != CONFIG_TYPE_INT || v.v.integer < 0 ||
	   v.v.integer > 0xffffff) {
		config_value_free(&v);
		return 0;
	}
	{
		std::uint32_t c = (std::uint32_t)v.v.integer;
		config_value_free(&v);
		return c;
	}
}

/* Read a positive number (int or float) key in points; def on error. */
static double read_pt(const char *path, const char *key, double def)
{
	config_value_t v;

	if(config_read_file(path, key, &v)) {
		return def;
	}
	switch(v.type) {
	case CONFIG_TYPE_INT:
		if(v.v.integer >= 0) {
			double d = (double)v.v.integer;
			config_value_free(&v);
			return d;
		}
		break;
	case CONFIG_TYPE_FLOAT:
		if(v.v.floating >= 0) {
			double d = v.v.floating;
			config_value_free(&v);
			return d;
		}
		break;
	default:
		break;
	}
	config_value_free(&v);
	return def;
}

/* Read a string key into buf (buflen); true when present. */
static bool read_string(const char *path, const char *key,
			char *buf, size_t buflen)
{
	config_value_t v;

	if(config_read_file(path, key, &v)) {
		return false;
	}
	if(v.type != CONFIG_TYPE_STRING || !v.v.string) {
		config_value_free(&v);
		return false;
	}
	snprintf(buf, buflen, "%s", v.v.string);
	config_value_free(&v);
	return true;
}

/* ---- Theme -------------------------------------------------------- */

Theme::Theme() : impl_(new Impl()) { }
Theme::~Theme() { delete impl_; }

bool
Theme::load()
{
	const char *active = DEF_NAME;
	char path[512] = "";

	impl_->loaded = false;

	/* 1) the active theme name from the system.theme domain (system ->
	 * user -> shared; absent -> the shipped "Argentum" name) */
	if(!config_get_string(THEME_CONF_DOMAIN, "active", &active) ||
	   !active || !*active || strlen(active) >= sizeof(impl_->name)) {
		active = DEF_NAME;
	}
	snprintf(impl_->name, sizeof(impl_->name), "%s", active);
	snprintf(path, sizeof(path), "%s/%s.conf", THEMES_DIR, active);

	/* 2) parse the theme file (plain raw-file read). A missing file
	 * or any unreadable key falls back to the compiled defaults. */
	snprintf(impl_->name, sizeof(impl_->name), "%s", active);

	/* palette */
	impl_->accent = read_color(path, "accent");
	if(!impl_->accent) {
		impl_->accent = DEF_ACCENT;
	}
	impl_->chromeTop = read_color(path, "chrome.top");
	if(!impl_->chromeTop) {
		impl_->chromeTop = DEF_CHROME_TOP;
	}
	impl_->chromeBottom = read_color(path, "chrome.bottom");
	if(!impl_->chromeBottom) {
		impl_->chromeBottom = DEF_CHROME_BOT;
	}
	impl_->page = read_color(path, "page");
	if(!impl_->page) {
		impl_->page = DEF_PAGE;
	}
	impl_->text = read_color(path, "text");
	if(!impl_->text) {
		impl_->text = DEF_TEXT;
	}

	/* geometry (points) */
	impl_->smallRadius = read_pt(path, "radius.small", DEF_RAD_SMALL);
	impl_->baseRadius = read_pt(path, "radius.base", DEF_RAD_BASE);
	impl_->bevel = read_pt(path, "bevel", DEF_BEVEL);
	impl_->outline = read_pt(path, "outline", DEF_OUTLINE);

	/* font selection */
	if(!read_string(path, "font.family", impl_->fontFamily,
			sizeof(impl_->fontFamily))) {
		snprintf(impl_->fontFamily, sizeof(impl_->fontFamily),
			 "%s", DEF_FONT_FAMILY);
	}
	impl_->fontPt = read_pt(path, "font.size", DEF_FONT_PT);

	/* 3) the derived colours: file `derived` overrides win, else the
	 * accent derivation below (plan §3: one accent in, states out). */
	{
		std::uint32_t co = read_color(path, "derived.chrome_outline");

		impl_->chromeOutline = co ? co
					  : darken(impl_->accent, 0.55);
	}
	{
		/* idle: chrome surface, dark-accent outline, body text */
		std::uint32_t f = read_color(path, "derived.idle_fill");

		impl_->states[(int)ControlState::Idle].fillTop =
			f ? lighten(f, 0.08) : impl_->chromeTop;
		impl_->states[(int)ControlState::Idle].fillBottom =
			f ? f : impl_->chromeBottom;
		impl_->states[(int)ControlState::Idle].outline =
			impl_->chromeOutline;
		impl_->states[(int)ControlState::Idle].label = impl_->text;
	}
	{
		/* hover: chrome tinted toward the accent */
		std::uint32_t f = read_color(path, "derived.hover_fill");

		impl_->states[(int)ControlState::Hover].fillTop =
			f ? lighten(f, 0.12)
			  : mix(impl_->chromeTop, impl_->accent, 0.25);
		impl_->states[(int)ControlState::Hover].fillBottom =
			f ? darken(f, 0.08)
			  : mix(impl_->chromeBottom, impl_->accent, 0.35);
		impl_->states[(int)ControlState::Hover].outline =
			impl_->chromeOutline;
		impl_->states[(int)ControlState::Hover].label = impl_->text;
	}
	{
		/* armed/active: the accent fill (periwinkle), white label */
		std::uint32_t f = read_color(path, "derived.armed_fill");

		impl_->states[(int)ControlState::Armed].fillTop =
			f ? lighten(f, 0.10) : lighten(impl_->accent, 0.10);
		impl_->states[(int)ControlState::Armed].fillBottom =
			f ? f : impl_->accent;
		impl_->states[(int)ControlState::Armed].outline =
			impl_->chromeOutline;
		impl_->states[(int)ControlState::Armed].label = 0xffffff;
	}
	{
		/* disabled: desaturated chrome, muted label */
		std::uint32_t f = read_color(path, "derived.disabled_fill");

		impl_->states[(int)ControlState::Disabled].fillTop =
			f ? f : desaturate(impl_->chromeTop, 0.55);
		impl_->states[(int)ControlState::Disabled].fillBottom =
			f ? darken(f, 0.05)
			  : desaturate(impl_->chromeBottom, 0.55);
		impl_->states[(int)ControlState::Disabled].outline =
			desaturate(impl_->chromeOutline, 0.6);
		impl_->states[(int)ControlState::Disabled].label =
			mix(impl_->text, 0xffffff, 0.45);
	}
	{
		/* focused: idle chrome + accent outline (the focus ring) */
		std::uint32_t f = read_color(path, "derived.focused_fill");

		impl_->states[(int)ControlState::Focused].fillTop =
			f ? lighten(f, 0.05) : impl_->chromeTop;
		impl_->states[(int)ControlState::Focused].fillBottom =
			f ? f : impl_->chromeBottom;
		impl_->states[(int)ControlState::Focused].outline =
			impl_->accent;
		impl_->states[(int)ControlState::Focused].label = impl_->text;
	}

	impl_->loaded = true;
	return true;
}

bool
Theme::valid() const
{
	return impl_->loaded;
}

const char *
Theme::name() const
{
	return impl_->name;
}

std::uint32_t Theme::accent() const { return impl_->accent; }
std::uint32_t Theme::chromeTop() const { return impl_->chromeTop; }
std::uint32_t Theme::chromeBottom() const { return impl_->chromeBottom; }
std::uint32_t Theme::page() const { return impl_->page; }
std::uint32_t Theme::text() const { return impl_->text; }

double Theme::smallRadius() const { return impl_->smallRadius; }
double Theme::baseRadius() const { return impl_->baseRadius; }
double Theme::bevel() const { return impl_->bevel; }
double Theme::outline() const { return impl_->outline; }

const char *
Theme::fontFamily() const
{
	return impl_->fontFamily;
}

double
Theme::fontSizePt() const
{
	return impl_->fontPt;
}

Theme::Params
Theme::state(ControlState s) const
{
	int i = (int)s;

	if(i < 0 || i > (int)ControlState::Focused) {
		i = (int)ControlState::Idle;
	}
	Params p;
	p.fillTop = impl_->states[i].fillTop;
	p.fillBottom = impl_->states[i].fillBottom;
	p.outline = impl_->states[i].outline;
	p.label = impl_->states[i].label;
	return p;
}

std::uint32_t
Theme::chromeOutline() const
{
	return impl_->chromeOutline;
}

} /* namespace argentum */
