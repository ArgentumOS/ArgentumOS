# Standalone Xvfb build (no meson/ninja/autotools).
# Sources extracted from xserver 21.1.24 into this tree by
# ../../../tools/x11-extract-xvfb.py (layout preserved, so relative
# #include "..." works). Generated config headers (dix-config.h, xkb-config.h,
# xorg-config.h, version-config.h, xorg-server.h) live in include/.
# Third-party libs come from the static-musl prefix set via X11PREFIX.

CC        ?= gcc
X11PREFIX ?= $(CURDIR)/../../../.build/x11-prefix
OUT       ?= $(CURDIR)/build
BUILD     := $(OUT)/obj
CFLAGS    ?= -std=gnu99 -O2 -g -fno-strict-aliasing -pthread
LDFLAGS   ?=

INC  = -Iinclude
INC += -IXext
INC += -IXi
INC += -Icomposite
INC += -Idamageext
INC += -Idbe
INC += -Idix
INC += -Ifb
INC += -Ihw/vfb
INC += -Imi
INC += -Imiext/damage
INC += -Imiext/sync
INC += -Ios
INC += -Ipresent
INC += -Irandr
INC += -Irecord
INC += -Irender
INC += -Ixfixes
INC += -Ixkb
INC += -I$(X11PREFIX)/include -I$(X11PREFIX)/include/pixman-1
DEFS  = -DHAVE_DIX_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_DEFAULT_SOURCE
DEFS += -D_BSD_SOURCE -DHAS_FCHOWN -DHAS_STICKY_DIR_BIT
CPPFLAGS = $(DEFS) $(INC)

.PHONY: all clean
all: $(OUT)/Xvfb

$(OUT)/Xvfb: $(BUILD)/libxserver_main.a $(BUILD)/libxserver_fb.a $(BUILD)/libxserver_mi.a $(BUILD)/libxserver_dix.a $(BUILD)/libxserver_composite.a $(BUILD)/libxserver_damageext.a $(BUILD)/libxserver_dbe.a $(BUILD)/libxserver_randr.a $(BUILD)/libxserver_miext_damage.a $(BUILD)/libxserver_render.a $(BUILD)/libxserver_present.a $(BUILD)/libxserver_xext.a $(BUILD)/libxserver_miext_sync.a $(BUILD)/libxserver_xfixes.a $(BUILD)/libxserver_xi.a $(BUILD)/libxserver_xkb.a $(BUILD)/libxserver_record.a $(BUILD)/libxserver_os.a $(BUILD)/libxlibc.a $(BUILD)/libxserver_xkb_stubs.a $(BUILD)/libxserver_xi_stubs.a $(BUILD)/libxvfb.a
	$(CC) $(LDFLAGS) -o $@ -Wl,--start-group \
		$(BUILD)/libxserver_main.a \
		$(BUILD)/libxserver_fb.a \
		$(BUILD)/libxserver_mi.a \
		$(BUILD)/libxserver_dix.a \
		$(BUILD)/libxserver_composite.a \
		$(BUILD)/libxserver_damageext.a \
		$(BUILD)/libxserver_dbe.a \
		$(BUILD)/libxserver_randr.a \
		$(BUILD)/libxserver_miext_damage.a \
		$(BUILD)/libxserver_render.a \
		$(BUILD)/libxserver_present.a \
		$(BUILD)/libxserver_xext.a \
		$(BUILD)/libxserver_miext_sync.a \
		$(BUILD)/libxserver_xfixes.a \
		$(BUILD)/libxserver_xi.a \
		$(BUILD)/libxserver_xkb.a \
		$(BUILD)/libxserver_record.a \
		$(BUILD)/libxserver_os.a \
		$(BUILD)/libxlibc.a \
		$(BUILD)/libxserver_xkb_stubs.a \
		$(BUILD)/libxserver_xi_stubs.a \
		$(BUILD)/libxvfb.a \
		$(X11PREFIX)/lib/libpixman-1.a \
		$(X11PREFIX)/lib/libxkbfile.a \
		$(X11PREFIX)/lib/libXfont2.a \
		$(X11PREFIX)/lib/libsha1.a \
		$(X11PREFIX)/lib/libXau.a \
		$(X11PREFIX)/lib/libXdmcp.a \
		-Wl,--end-group -lm -ldl -lpthread

clean:
	rm -rf $(OUT)

$(BUILD)/libxserver_main.a: $(BUILD)/xserver_main__dix_stubmain.c.o
	ar rcs $@ $(BUILD)/xserver_main__dix_stubmain.c.o
$(BUILD)/libxserver_fb.a: $(BUILD)/xserver_fb__fb_fballpriv.c.o $(BUILD)/xserver_fb__fb_fbarc.c.o $(BUILD)/xserver_fb__fb_fbbits.c.o $(BUILD)/xserver_fb__fb_fbblt.c.o $(BUILD)/xserver_fb__fb_fbbltone.c.o $(BUILD)/xserver_fb__fb_fbcmap_mi.c.o $(BUILD)/xserver_fb__fb_fbcopy.c.o $(BUILD)/xserver_fb__fb_fbfill.c.o $(BUILD)/xserver_fb__fb_fbfillrect.c.o $(BUILD)/xserver_fb__fb_fbfillsp.c.o $(BUILD)/xserver_fb__fb_fbgc.c.o $(BUILD)/xserver_fb__fb_fbgetsp.c.o $(BUILD)/xserver_fb__fb_fbglyph.c.o $(BUILD)/xserver_fb__fb_fbimage.c.o $(BUILD)/xserver_fb__fb_fbline.c.o $(BUILD)/xserver_fb__fb_fboverlay.c.o $(BUILD)/xserver_fb__fb_fbpict.c.o $(BUILD)/xserver_fb__fb_fbpixmap.c.o $(BUILD)/xserver_fb__fb_fbpoint.c.o $(BUILD)/xserver_fb__fb_fbpush.c.o $(BUILD)/xserver_fb__fb_fbscreen.c.o $(BUILD)/xserver_fb__fb_fbseg.c.o $(BUILD)/xserver_fb__fb_fbsetsp.c.o $(BUILD)/xserver_fb__fb_fbsolid.c.o $(BUILD)/xserver_fb__fb_fbtrap.c.o $(BUILD)/xserver_fb__fb_fbutil.c.o $(BUILD)/xserver_fb__fb_fbwindow.c.o
	ar rcs $@ $(BUILD)/xserver_fb__fb_fballpriv.c.o $(BUILD)/xserver_fb__fb_fbarc.c.o $(BUILD)/xserver_fb__fb_fbbits.c.o $(BUILD)/xserver_fb__fb_fbblt.c.o $(BUILD)/xserver_fb__fb_fbbltone.c.o $(BUILD)/xserver_fb__fb_fbcmap_mi.c.o $(BUILD)/xserver_fb__fb_fbcopy.c.o $(BUILD)/xserver_fb__fb_fbfill.c.o $(BUILD)/xserver_fb__fb_fbfillrect.c.o $(BUILD)/xserver_fb__fb_fbfillsp.c.o $(BUILD)/xserver_fb__fb_fbgc.c.o $(BUILD)/xserver_fb__fb_fbgetsp.c.o $(BUILD)/xserver_fb__fb_fbglyph.c.o $(BUILD)/xserver_fb__fb_fbimage.c.o $(BUILD)/xserver_fb__fb_fbline.c.o $(BUILD)/xserver_fb__fb_fboverlay.c.o $(BUILD)/xserver_fb__fb_fbpict.c.o $(BUILD)/xserver_fb__fb_fbpixmap.c.o $(BUILD)/xserver_fb__fb_fbpoint.c.o $(BUILD)/xserver_fb__fb_fbpush.c.o $(BUILD)/xserver_fb__fb_fbscreen.c.o $(BUILD)/xserver_fb__fb_fbseg.c.o $(BUILD)/xserver_fb__fb_fbsetsp.c.o $(BUILD)/xserver_fb__fb_fbsolid.c.o $(BUILD)/xserver_fb__fb_fbtrap.c.o $(BUILD)/xserver_fb__fb_fbutil.c.o $(BUILD)/xserver_fb__fb_fbwindow.c.o
$(BUILD)/libxserver_mi.a: $(BUILD)/xserver_mi__mi_miarc.c.o $(BUILD)/xserver_mi__mi_mibitblt.c.o $(BUILD)/xserver_mi__mi_micmap.c.o $(BUILD)/xserver_mi__mi_micopy.c.o $(BUILD)/xserver_mi__mi_midash.c.o $(BUILD)/xserver_mi__mi_midispcur.c.o $(BUILD)/xserver_mi__mi_mieq.c.o $(BUILD)/xserver_mi__mi_miexpose.c.o $(BUILD)/xserver_mi__mi_mifillarc.c.o $(BUILD)/xserver_mi__mi_mifillrct.c.o $(BUILD)/xserver_mi__mi_migc.c.o $(BUILD)/xserver_mi__mi_miglblt.c.o $(BUILD)/xserver_mi__mi_mioverlay.c.o $(BUILD)/xserver_mi__mi_mipointer.c.o $(BUILD)/xserver_mi__mi_mipoly.c.o $(BUILD)/xserver_mi__mi_mipolypnt.c.o $(BUILD)/xserver_mi__mi_mipolyrect.c.o $(BUILD)/xserver_mi__mi_mipolyseg.c.o $(BUILD)/xserver_mi__mi_mipolytext.c.o $(BUILD)/xserver_mi__mi_mipushpxl.c.o $(BUILD)/xserver_mi__mi_miscrinit.c.o $(BUILD)/xserver_mi__mi_misprite.c.o $(BUILD)/xserver_mi__mi_mivaltree.c.o $(BUILD)/xserver_mi__mi_miwideline.c.o $(BUILD)/xserver_mi__mi_miwindow.c.o $(BUILD)/xserver_mi__mi_mizerarc.c.o $(BUILD)/xserver_mi__mi_mizerclip.c.o $(BUILD)/xserver_mi__mi_mizerline.c.o
	ar rcs $@ $(BUILD)/xserver_mi__mi_miarc.c.o $(BUILD)/xserver_mi__mi_mibitblt.c.o $(BUILD)/xserver_mi__mi_micmap.c.o $(BUILD)/xserver_mi__mi_micopy.c.o $(BUILD)/xserver_mi__mi_midash.c.o $(BUILD)/xserver_mi__mi_midispcur.c.o $(BUILD)/xserver_mi__mi_mieq.c.o $(BUILD)/xserver_mi__mi_miexpose.c.o $(BUILD)/xserver_mi__mi_mifillarc.c.o $(BUILD)/xserver_mi__mi_mifillrct.c.o $(BUILD)/xserver_mi__mi_migc.c.o $(BUILD)/xserver_mi__mi_miglblt.c.o $(BUILD)/xserver_mi__mi_mioverlay.c.o $(BUILD)/xserver_mi__mi_mipointer.c.o $(BUILD)/xserver_mi__mi_mipoly.c.o $(BUILD)/xserver_mi__mi_mipolypnt.c.o $(BUILD)/xserver_mi__mi_mipolyrect.c.o $(BUILD)/xserver_mi__mi_mipolyseg.c.o $(BUILD)/xserver_mi__mi_mipolytext.c.o $(BUILD)/xserver_mi__mi_mipushpxl.c.o $(BUILD)/xserver_mi__mi_miscrinit.c.o $(BUILD)/xserver_mi__mi_misprite.c.o $(BUILD)/xserver_mi__mi_mivaltree.c.o $(BUILD)/xserver_mi__mi_miwideline.c.o $(BUILD)/xserver_mi__mi_miwindow.c.o $(BUILD)/xserver_mi__mi_mizerarc.c.o $(BUILD)/xserver_mi__mi_mizerclip.c.o $(BUILD)/xserver_mi__mi_mizerline.c.o
$(BUILD)/libxserver_dix.a: $(BUILD)/xserver_dix__dix_atom.c.o $(BUILD)/xserver_dix__dix_colormap.c.o $(BUILD)/xserver_dix__dix_cursor.c.o $(BUILD)/xserver_dix__dix_devices.c.o $(BUILD)/xserver_dix__dix_dispatch.c.o $(BUILD)/xserver_dix__dix_dixfonts.c.o $(BUILD)/xserver_dix__dix_main.c.o $(BUILD)/xserver_dix__dix_dixutils.c.o $(BUILD)/xserver_dix__dix_enterleave.c.o $(BUILD)/xserver_dix__dix_events.c.o $(BUILD)/xserver_dix__dix_eventconvert.c.o $(BUILD)/xserver_dix__dix_extension.c.o $(BUILD)/xserver_dix__dix_gc.c.o $(BUILD)/xserver_dix__dix_gestures.c.o $(BUILD)/xserver_dix__dix_getevents.c.o $(BUILD)/xserver_dix__dix_globals.c.o $(BUILD)/xserver_dix__dix_glyphcurs.c.o $(BUILD)/xserver_dix__dix_grabs.c.o $(BUILD)/xserver_dix__dix_initatoms.c.o $(BUILD)/xserver_dix__dix_inpututils.c.o $(BUILD)/xserver_dix__dix_pixmap.c.o $(BUILD)/xserver_dix__dix_privates.c.o $(BUILD)/xserver_dix__dix_property.c.o $(BUILD)/xserver_dix__dix_ptrveloc.c.o $(BUILD)/xserver_dix__dix_region.c.o $(BUILD)/xserver_dix__dix_registry.c.o $(BUILD)/xserver_dix__dix_resource.c.o $(BUILD)/xserver_dix__dix_selection.c.o $(BUILD)/xserver_dix__dix_swaprep.c.o $(BUILD)/xserver_dix__dix_swapreq.c.o $(BUILD)/xserver_dix__dix_tables.c.o $(BUILD)/xserver_dix__dix_touch.c.o $(BUILD)/xserver_dix__dix_window.c.o
	ar rcs $@ $(BUILD)/xserver_dix__dix_atom.c.o $(BUILD)/xserver_dix__dix_colormap.c.o $(BUILD)/xserver_dix__dix_cursor.c.o $(BUILD)/xserver_dix__dix_devices.c.o $(BUILD)/xserver_dix__dix_dispatch.c.o $(BUILD)/xserver_dix__dix_dixfonts.c.o $(BUILD)/xserver_dix__dix_main.c.o $(BUILD)/xserver_dix__dix_dixutils.c.o $(BUILD)/xserver_dix__dix_enterleave.c.o $(BUILD)/xserver_dix__dix_events.c.o $(BUILD)/xserver_dix__dix_eventconvert.c.o $(BUILD)/xserver_dix__dix_extension.c.o $(BUILD)/xserver_dix__dix_gc.c.o $(BUILD)/xserver_dix__dix_gestures.c.o $(BUILD)/xserver_dix__dix_getevents.c.o $(BUILD)/xserver_dix__dix_globals.c.o $(BUILD)/xserver_dix__dix_glyphcurs.c.o $(BUILD)/xserver_dix__dix_grabs.c.o $(BUILD)/xserver_dix__dix_initatoms.c.o $(BUILD)/xserver_dix__dix_inpututils.c.o $(BUILD)/xserver_dix__dix_pixmap.c.o $(BUILD)/xserver_dix__dix_privates.c.o $(BUILD)/xserver_dix__dix_property.c.o $(BUILD)/xserver_dix__dix_ptrveloc.c.o $(BUILD)/xserver_dix__dix_region.c.o $(BUILD)/xserver_dix__dix_registry.c.o $(BUILD)/xserver_dix__dix_resource.c.o $(BUILD)/xserver_dix__dix_selection.c.o $(BUILD)/xserver_dix__dix_swaprep.c.o $(BUILD)/xserver_dix__dix_swapreq.c.o $(BUILD)/xserver_dix__dix_tables.c.o $(BUILD)/xserver_dix__dix_touch.c.o $(BUILD)/xserver_dix__dix_window.c.o
$(BUILD)/libxserver_composite.a: $(BUILD)/xserver_composite__composite_compalloc.c.o $(BUILD)/xserver_composite__composite_compext.c.o $(BUILD)/xserver_composite__composite_compinit.c.o $(BUILD)/xserver_composite__composite_compoverlay.c.o $(BUILD)/xserver_composite__composite_compwindow.c.o
	ar rcs $@ $(BUILD)/xserver_composite__composite_compalloc.c.o $(BUILD)/xserver_composite__composite_compext.c.o $(BUILD)/xserver_composite__composite_compinit.c.o $(BUILD)/xserver_composite__composite_compoverlay.c.o $(BUILD)/xserver_composite__composite_compwindow.c.o
$(BUILD)/libxserver_damageext.a: $(BUILD)/xserver_damageext__damageext_damageext.c.o
	ar rcs $@ $(BUILD)/xserver_damageext__damageext_damageext.c.o
$(BUILD)/libxserver_dbe.a: $(BUILD)/xserver_dbe__dbe_dbe.c.o $(BUILD)/xserver_dbe__dbe_midbe.c.o
	ar rcs $@ $(BUILD)/xserver_dbe__dbe_dbe.c.o $(BUILD)/xserver_dbe__dbe_midbe.c.o
$(BUILD)/libxserver_randr.a: $(BUILD)/xserver_randr__randr_randr.c.o $(BUILD)/xserver_randr__randr_rrcrtc.c.o $(BUILD)/xserver_randr__randr_rrdispatch.c.o $(BUILD)/xserver_randr__randr_rrinfo.c.o $(BUILD)/xserver_randr__randr_rrlease.c.o $(BUILD)/xserver_randr__randr_rrmode.c.o $(BUILD)/xserver_randr__randr_rrmonitor.c.o $(BUILD)/xserver_randr__randr_rroutput.c.o $(BUILD)/xserver_randr__randr_rrpointer.c.o $(BUILD)/xserver_randr__randr_rrproperty.c.o $(BUILD)/xserver_randr__randr_rrprovider.c.o $(BUILD)/xserver_randr__randr_rrproviderproperty.c.o $(BUILD)/xserver_randr__randr_rrscreen.c.o $(BUILD)/xserver_randr__randr_rrsdispatch.c.o $(BUILD)/xserver_randr__randr_rrtransform.c.o $(BUILD)/xserver_randr__randr_rrxinerama.c.o
	ar rcs $@ $(BUILD)/xserver_randr__randr_randr.c.o $(BUILD)/xserver_randr__randr_rrcrtc.c.o $(BUILD)/xserver_randr__randr_rrdispatch.c.o $(BUILD)/xserver_randr__randr_rrinfo.c.o $(BUILD)/xserver_randr__randr_rrlease.c.o $(BUILD)/xserver_randr__randr_rrmode.c.o $(BUILD)/xserver_randr__randr_rrmonitor.c.o $(BUILD)/xserver_randr__randr_rroutput.c.o $(BUILD)/xserver_randr__randr_rrpointer.c.o $(BUILD)/xserver_randr__randr_rrproperty.c.o $(BUILD)/xserver_randr__randr_rrprovider.c.o $(BUILD)/xserver_randr__randr_rrproviderproperty.c.o $(BUILD)/xserver_randr__randr_rrscreen.c.o $(BUILD)/xserver_randr__randr_rrsdispatch.c.o $(BUILD)/xserver_randr__randr_rrtransform.c.o $(BUILD)/xserver_randr__randr_rrxinerama.c.o
$(BUILD)/libxserver_miext_damage.a: $(BUILD)/xserver_miext_damage__miext_damage_damage.c.o
	ar rcs $@ $(BUILD)/xserver_miext_damage__miext_damage_damage.c.o
$(BUILD)/libxserver_render.a: $(BUILD)/xserver_render__render_animcur.c.o $(BUILD)/xserver_render__render_filter.c.o $(BUILD)/xserver_render__render_glyph.c.o $(BUILD)/xserver_render__render_matrix.c.o $(BUILD)/xserver_render__render_miindex.c.o $(BUILD)/xserver_render__render_mipict.c.o $(BUILD)/xserver_render__render_mirect.c.o $(BUILD)/xserver_render__render_mitrap.c.o $(BUILD)/xserver_render__render_mitri.c.o $(BUILD)/xserver_render__render_picture.c.o $(BUILD)/xserver_render__render_render.c.o
	ar rcs $@ $(BUILD)/xserver_render__render_animcur.c.o $(BUILD)/xserver_render__render_filter.c.o $(BUILD)/xserver_render__render_glyph.c.o $(BUILD)/xserver_render__render_matrix.c.o $(BUILD)/xserver_render__render_miindex.c.o $(BUILD)/xserver_render__render_mipict.c.o $(BUILD)/xserver_render__render_mirect.c.o $(BUILD)/xserver_render__render_mitrap.c.o $(BUILD)/xserver_render__render_mitri.c.o $(BUILD)/xserver_render__render_picture.c.o $(BUILD)/xserver_render__render_render.c.o
$(BUILD)/libxserver_present.a: $(BUILD)/xserver_present__present_present.c.o $(BUILD)/xserver_present__present_present_event.c.o $(BUILD)/xserver_present__present_present_execute.c.o $(BUILD)/xserver_present__present_present_fake.c.o $(BUILD)/xserver_present__present_present_fence.c.o $(BUILD)/xserver_present__present_present_notify.c.o $(BUILD)/xserver_present__present_present_request.c.o $(BUILD)/xserver_present__present_present_scmd.c.o $(BUILD)/xserver_present__present_present_screen.c.o $(BUILD)/xserver_present__present_present_vblank.c.o
	ar rcs $@ $(BUILD)/xserver_present__present_present.c.o $(BUILD)/xserver_present__present_present_event.c.o $(BUILD)/xserver_present__present_present_execute.c.o $(BUILD)/xserver_present__present_present_fake.c.o $(BUILD)/xserver_present__present_present_fence.c.o $(BUILD)/xserver_present__present_present_notify.c.o $(BUILD)/xserver_present__present_present_request.c.o $(BUILD)/xserver_present__present_present_scmd.c.o $(BUILD)/xserver_present__present_present_screen.c.o $(BUILD)/xserver_present__present_present_vblank.c.o
$(BUILD)/libxserver_xext.a: $(BUILD)/xserver_xext__Xext_bigreq.c.o $(BUILD)/xserver_xext__Xext_geext.c.o $(BUILD)/xserver_xext__Xext_shape.c.o $(BUILD)/xserver_xext__Xext_sleepuntil.c.o $(BUILD)/xserver_xext__Xext_sync.c.o $(BUILD)/xserver_xext__Xext_xcmisc.c.o $(BUILD)/xserver_xext__Xext_xtest.c.o $(BUILD)/xserver_xext__Xext_dpms.c.o $(BUILD)/xserver_xext__Xext_shm.c.o $(BUILD)/xserver_xext__Xext_hashtable.c.o $(BUILD)/xserver_xext__Xext_xres.c.o $(BUILD)/xserver_xext__Xext_saver.c.o $(BUILD)/xserver_xext__Xext_xace.c.o $(BUILD)/xserver_xext__Xext_panoramiX.c.o $(BUILD)/xserver_xext__Xext_panoramiXprocs.c.o $(BUILD)/xserver_xext__Xext_panoramiXSwap.c.o $(BUILD)/xserver_xext__Xext_xvmain.c.o $(BUILD)/xserver_xext__Xext_xvdisp.c.o $(BUILD)/xserver_xext__Xext_xvmc.c.o
	ar rcs $@ $(BUILD)/xserver_xext__Xext_bigreq.c.o $(BUILD)/xserver_xext__Xext_geext.c.o $(BUILD)/xserver_xext__Xext_shape.c.o $(BUILD)/xserver_xext__Xext_sleepuntil.c.o $(BUILD)/xserver_xext__Xext_sync.c.o $(BUILD)/xserver_xext__Xext_xcmisc.c.o $(BUILD)/xserver_xext__Xext_xtest.c.o $(BUILD)/xserver_xext__Xext_dpms.c.o $(BUILD)/xserver_xext__Xext_shm.c.o $(BUILD)/xserver_xext__Xext_hashtable.c.o $(BUILD)/xserver_xext__Xext_xres.c.o $(BUILD)/xserver_xext__Xext_saver.c.o $(BUILD)/xserver_xext__Xext_xace.c.o $(BUILD)/xserver_xext__Xext_panoramiX.c.o $(BUILD)/xserver_xext__Xext_panoramiXprocs.c.o $(BUILD)/xserver_xext__Xext_panoramiXSwap.c.o $(BUILD)/xserver_xext__Xext_xvmain.c.o $(BUILD)/xserver_xext__Xext_xvdisp.c.o $(BUILD)/xserver_xext__Xext_xvmc.c.o
$(BUILD)/libxserver_miext_sync.a: $(BUILD)/xserver_miext_sync__miext_sync_misync.c.o $(BUILD)/xserver_miext_sync__miext_sync_misyncfd.c.o
	ar rcs $@ $(BUILD)/xserver_miext_sync__miext_sync_misync.c.o $(BUILD)/xserver_miext_sync__miext_sync_misyncfd.c.o
$(BUILD)/libxserver_xfixes.a: $(BUILD)/xserver_xfixes__xfixes_cursor.c.o $(BUILD)/xserver_xfixes__xfixes_disconnect.c.o $(BUILD)/xserver_xfixes__xfixes_region.c.o $(BUILD)/xserver_xfixes__xfixes_saveset.c.o $(BUILD)/xserver_xfixes__xfixes_select.c.o $(BUILD)/xserver_xfixes__xfixes_xfixes.c.o
	ar rcs $@ $(BUILD)/xserver_xfixes__xfixes_cursor.c.o $(BUILD)/xserver_xfixes__xfixes_disconnect.c.o $(BUILD)/xserver_xfixes__xfixes_region.c.o $(BUILD)/xserver_xfixes__xfixes_saveset.c.o $(BUILD)/xserver_xfixes__xfixes_select.c.o $(BUILD)/xserver_xfixes__xfixes_xfixes.c.o
$(BUILD)/libxserver_xi.a: $(BUILD)/xserver_xi__Xi_allowev.c.o $(BUILD)/xserver_xi__Xi_chgdctl.c.o $(BUILD)/xserver_xi__Xi_chgfctl.c.o $(BUILD)/xserver_xi__Xi_chgkbd.c.o $(BUILD)/xserver_xi__Xi_chgkmap.c.o $(BUILD)/xserver_xi__Xi_chgprop.c.o $(BUILD)/xserver_xi__Xi_chgptr.c.o $(BUILD)/xserver_xi__Xi_closedev.c.o $(BUILD)/xserver_xi__Xi_devbell.c.o $(BUILD)/xserver_xi__Xi_exevents.c.o $(BUILD)/xserver_xi__Xi_extinit.c.o $(BUILD)/xserver_xi__Xi_getbmap.c.o $(BUILD)/xserver_xi__Xi_getdctl.c.o $(BUILD)/xserver_xi__Xi_getfctl.c.o $(BUILD)/xserver_xi__Xi_getfocus.c.o $(BUILD)/xserver_xi__Xi_getkmap.c.o $(BUILD)/xserver_xi__Xi_getmmap.c.o $(BUILD)/xserver_xi__Xi_getprop.c.o $(BUILD)/xserver_xi__Xi_getselev.c.o $(BUILD)/xserver_xi__Xi_getvers.c.o $(BUILD)/xserver_xi__Xi_grabdev.c.o $(BUILD)/xserver_xi__Xi_grabdevb.c.o $(BUILD)/xserver_xi__Xi_grabdevk.c.o $(BUILD)/xserver_xi__Xi_gtmotion.c.o $(BUILD)/xserver_xi__Xi_listdev.c.o $(BUILD)/xserver_xi__Xi_opendev.c.o $(BUILD)/xserver_xi__Xi_queryst.c.o $(BUILD)/xserver_xi__Xi_selectev.c.o $(BUILD)/xserver_xi__Xi_sendexev.c.o $(BUILD)/xserver_xi__Xi_setbmap.c.o $(BUILD)/xserver_xi__Xi_setdval.c.o $(BUILD)/xserver_xi__Xi_setfocus.c.o $(BUILD)/xserver_xi__Xi_setmmap.c.o $(BUILD)/xserver_xi__Xi_setmode.c.o $(BUILD)/xserver_xi__Xi_ungrdev.c.o $(BUILD)/xserver_xi__Xi_ungrdevb.c.o $(BUILD)/xserver_xi__Xi_ungrdevk.c.o $(BUILD)/xserver_xi__Xi_xiallowev.c.o $(BUILD)/xserver_xi__Xi_xibarriers.c.o $(BUILD)/xserver_xi__Xi_xichangecursor.c.o $(BUILD)/xserver_xi__Xi_xichangehierarchy.c.o $(BUILD)/xserver_xi__Xi_xigetclientpointer.c.o $(BUILD)/xserver_xi__Xi_xigrabdev.c.o $(BUILD)/xserver_xi__Xi_xipassivegrab.c.o $(BUILD)/xserver_xi__Xi_xiproperty.c.o $(BUILD)/xserver_xi__Xi_xiquerydevice.c.o $(BUILD)/xserver_xi__Xi_xiquerypointer.c.o $(BUILD)/xserver_xi__Xi_xiqueryversion.c.o $(BUILD)/xserver_xi__Xi_xiselectev.c.o $(BUILD)/xserver_xi__Xi_xisetclientpointer.c.o $(BUILD)/xserver_xi__Xi_xisetdevfocus.c.o $(BUILD)/xserver_xi__Xi_xiwarppointer.c.o
	ar rcs $@ $(BUILD)/xserver_xi__Xi_allowev.c.o $(BUILD)/xserver_xi__Xi_chgdctl.c.o $(BUILD)/xserver_xi__Xi_chgfctl.c.o $(BUILD)/xserver_xi__Xi_chgkbd.c.o $(BUILD)/xserver_xi__Xi_chgkmap.c.o $(BUILD)/xserver_xi__Xi_chgprop.c.o $(BUILD)/xserver_xi__Xi_chgptr.c.o $(BUILD)/xserver_xi__Xi_closedev.c.o $(BUILD)/xserver_xi__Xi_devbell.c.o $(BUILD)/xserver_xi__Xi_exevents.c.o $(BUILD)/xserver_xi__Xi_extinit.c.o $(BUILD)/xserver_xi__Xi_getbmap.c.o $(BUILD)/xserver_xi__Xi_getdctl.c.o $(BUILD)/xserver_xi__Xi_getfctl.c.o $(BUILD)/xserver_xi__Xi_getfocus.c.o $(BUILD)/xserver_xi__Xi_getkmap.c.o $(BUILD)/xserver_xi__Xi_getmmap.c.o $(BUILD)/xserver_xi__Xi_getprop.c.o $(BUILD)/xserver_xi__Xi_getselev.c.o $(BUILD)/xserver_xi__Xi_getvers.c.o $(BUILD)/xserver_xi__Xi_grabdev.c.o $(BUILD)/xserver_xi__Xi_grabdevb.c.o $(BUILD)/xserver_xi__Xi_grabdevk.c.o $(BUILD)/xserver_xi__Xi_gtmotion.c.o $(BUILD)/xserver_xi__Xi_listdev.c.o $(BUILD)/xserver_xi__Xi_opendev.c.o $(BUILD)/xserver_xi__Xi_queryst.c.o $(BUILD)/xserver_xi__Xi_selectev.c.o $(BUILD)/xserver_xi__Xi_sendexev.c.o $(BUILD)/xserver_xi__Xi_setbmap.c.o $(BUILD)/xserver_xi__Xi_setdval.c.o $(BUILD)/xserver_xi__Xi_setfocus.c.o $(BUILD)/xserver_xi__Xi_setmmap.c.o $(BUILD)/xserver_xi__Xi_setmode.c.o $(BUILD)/xserver_xi__Xi_ungrdev.c.o $(BUILD)/xserver_xi__Xi_ungrdevb.c.o $(BUILD)/xserver_xi__Xi_ungrdevk.c.o $(BUILD)/xserver_xi__Xi_xiallowev.c.o $(BUILD)/xserver_xi__Xi_xibarriers.c.o $(BUILD)/xserver_xi__Xi_xichangecursor.c.o $(BUILD)/xserver_xi__Xi_xichangehierarchy.c.o $(BUILD)/xserver_xi__Xi_xigetclientpointer.c.o $(BUILD)/xserver_xi__Xi_xigrabdev.c.o $(BUILD)/xserver_xi__Xi_xipassivegrab.c.o $(BUILD)/xserver_xi__Xi_xiproperty.c.o $(BUILD)/xserver_xi__Xi_xiquerydevice.c.o $(BUILD)/xserver_xi__Xi_xiquerypointer.c.o $(BUILD)/xserver_xi__Xi_xiqueryversion.c.o $(BUILD)/xserver_xi__Xi_xiselectev.c.o $(BUILD)/xserver_xi__Xi_xisetclientpointer.c.o $(BUILD)/xserver_xi__Xi_xisetdevfocus.c.o $(BUILD)/xserver_xi__Xi_xiwarppointer.c.o
$(BUILD)/libxserver_xkb.a: $(BUILD)/xserver_xkb__xkb_ddxBeep.c.o $(BUILD)/xserver_xkb__xkb_ddxCtrls.c.o $(BUILD)/xserver_xkb__xkb_ddxLEDs.c.o $(BUILD)/xserver_xkb__xkb_ddxLoad.c.o $(BUILD)/xserver_xkb__xkb_maprules.c.o $(BUILD)/xserver_xkb__xkb_xkmread.c.o $(BUILD)/xserver_xkb__xkb_xkbtext.c.o $(BUILD)/xserver_xkb__xkb_xkbfmisc.c.o $(BUILD)/xserver_xkb__xkb_xkbout.c.o $(BUILD)/xserver_xkb__xkb_xkb.c.o $(BUILD)/xserver_xkb__xkb_xkbUtils.c.o $(BUILD)/xserver_xkb__xkb_xkbEvents.c.o $(BUILD)/xserver_xkb__xkb_xkbAccessX.c.o $(BUILD)/xserver_xkb__xkb_xkbSwap.c.o $(BUILD)/xserver_xkb__xkb_xkbLEDs.c.o $(BUILD)/xserver_xkb__xkb_xkbInit.c.o $(BUILD)/xserver_xkb__xkb_xkbActions.c.o $(BUILD)/xserver_xkb__xkb_xkbPrKeyEv.c.o $(BUILD)/xserver_xkb__xkb_XKBMisc.c.o $(BUILD)/xserver_xkb__xkb_XKBAlloc.c.o $(BUILD)/xserver_xkb__xkb_XKBGAlloc.c.o $(BUILD)/xserver_xkb__xkb_XKBMAlloc.c.o
	ar rcs $@ $(BUILD)/xserver_xkb__xkb_ddxBeep.c.o $(BUILD)/xserver_xkb__xkb_ddxCtrls.c.o $(BUILD)/xserver_xkb__xkb_ddxLEDs.c.o $(BUILD)/xserver_xkb__xkb_ddxLoad.c.o $(BUILD)/xserver_xkb__xkb_maprules.c.o $(BUILD)/xserver_xkb__xkb_xkmread.c.o $(BUILD)/xserver_xkb__xkb_xkbtext.c.o $(BUILD)/xserver_xkb__xkb_xkbfmisc.c.o $(BUILD)/xserver_xkb__xkb_xkbout.c.o $(BUILD)/xserver_xkb__xkb_xkb.c.o $(BUILD)/xserver_xkb__xkb_xkbUtils.c.o $(BUILD)/xserver_xkb__xkb_xkbEvents.c.o $(BUILD)/xserver_xkb__xkb_xkbAccessX.c.o $(BUILD)/xserver_xkb__xkb_xkbSwap.c.o $(BUILD)/xserver_xkb__xkb_xkbLEDs.c.o $(BUILD)/xserver_xkb__xkb_xkbInit.c.o $(BUILD)/xserver_xkb__xkb_xkbActions.c.o $(BUILD)/xserver_xkb__xkb_xkbPrKeyEv.c.o $(BUILD)/xserver_xkb__xkb_XKBMisc.c.o $(BUILD)/xserver_xkb__xkb_XKBAlloc.c.o $(BUILD)/xserver_xkb__xkb_XKBGAlloc.c.o $(BUILD)/xserver_xkb__xkb_XKBMAlloc.c.o
$(BUILD)/libxserver_record.a: $(BUILD)/xserver_record__record_record.c.o $(BUILD)/xserver_record__record_set.c.o
	ar rcs $@ $(BUILD)/xserver_record__record_record.c.o $(BUILD)/xserver_record__record_set.c.o
$(BUILD)/libxserver_os.a: $(BUILD)/xserver_os__os_WaitFor.c.o $(BUILD)/xserver_os__os_access.c.o $(BUILD)/xserver_os__os_auth.c.o $(BUILD)/xserver_os__os_backtrace.c.o $(BUILD)/xserver_os__os_client.c.o $(BUILD)/xserver_os__os_connection.c.o $(BUILD)/xserver_os__os_inputthread.c.o $(BUILD)/xserver_os__os_io.c.o $(BUILD)/xserver_os__os_mitauth.c.o $(BUILD)/xserver_os__os_oscolor.c.o $(BUILD)/xserver_os__os_osinit.c.o $(BUILD)/xserver_os__os_ospoll.c.o $(BUILD)/xserver_os__os_utils.c.o $(BUILD)/xserver_os__os_xdmauth.c.o $(BUILD)/xserver_os__os_xsha1.c.o $(BUILD)/xserver_os__os_xstrans.c.o $(BUILD)/xserver_os__os_xprintf.c.o $(BUILD)/xserver_os__os_log.c.o $(BUILD)/xserver_os__os_busfault.c.o
	ar rcs $@ $(BUILD)/xserver_os__os_WaitFor.c.o $(BUILD)/xserver_os__os_access.c.o $(BUILD)/xserver_os__os_auth.c.o $(BUILD)/xserver_os__os_backtrace.c.o $(BUILD)/xserver_os__os_client.c.o $(BUILD)/xserver_os__os_connection.c.o $(BUILD)/xserver_os__os_inputthread.c.o $(BUILD)/xserver_os__os_io.c.o $(BUILD)/xserver_os__os_mitauth.c.o $(BUILD)/xserver_os__os_oscolor.c.o $(BUILD)/xserver_os__os_osinit.c.o $(BUILD)/xserver_os__os_ospoll.c.o $(BUILD)/xserver_os__os_utils.c.o $(BUILD)/xserver_os__os_xdmauth.c.o $(BUILD)/xserver_os__os_xsha1.c.o $(BUILD)/xserver_os__os_xstrans.c.o $(BUILD)/xserver_os__os_xprintf.c.o $(BUILD)/xserver_os__os_log.c.o $(BUILD)/xserver_os__os_busfault.c.o
$(BUILD)/libxlibc.a: $(BUILD)/xlibc__os_timingsafe_memcmp.c.o
	ar rcs $@ $(BUILD)/xlibc__os_timingsafe_memcmp.c.o
$(BUILD)/libxserver_xkb_stubs.a: $(BUILD)/xserver_xkb_stubs__xkb_ddxKillSrv.c.o $(BUILD)/xserver_xkb_stubs__xkb_ddxPrivate.c.o $(BUILD)/xserver_xkb_stubs__xkb_ddxVT.c.o
	ar rcs $@ $(BUILD)/xserver_xkb_stubs__xkb_ddxKillSrv.c.o $(BUILD)/xserver_xkb_stubs__xkb_ddxPrivate.c.o $(BUILD)/xserver_xkb_stubs__xkb_ddxVT.c.o
$(BUILD)/libxserver_xi_stubs.a: $(BUILD)/xserver_xi_stubs__Xi_stubs.c.o
	ar rcs $@ $(BUILD)/xserver_xi_stubs__Xi_stubs.c.o
$(BUILD)/libxvfb.a: $(BUILD)/xvfb__hw_vfb_InitInput.c.o $(BUILD)/xvfb__hw_vfb_InitOutput.c.o $(BUILD)/xvfb__mi_miinitext.c.o
	ar rcs $@ $(BUILD)/xvfb__hw_vfb_InitInput.c.o $(BUILD)/xvfb__hw_vfb_InitOutput.c.o $(BUILD)/xvfb__mi_miinitext.c.o
$(BUILD)/xserver_main__dix_stubmain.c.o: dix/stubmain.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fballpriv.c.o: fb/fballpriv.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbarc.c.o: fb/fbarc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbbits.c.o: fb/fbbits.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbblt.c.o: fb/fbblt.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbbltone.c.o: fb/fbbltone.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbcmap_mi.c.o: fb/fbcmap_mi.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbcopy.c.o: fb/fbcopy.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbfill.c.o: fb/fbfill.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbfillrect.c.o: fb/fbfillrect.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbfillsp.c.o: fb/fbfillsp.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbgc.c.o: fb/fbgc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbgetsp.c.o: fb/fbgetsp.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbglyph.c.o: fb/fbglyph.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbimage.c.o: fb/fbimage.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbline.c.o: fb/fbline.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fboverlay.c.o: fb/fboverlay.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbpict.c.o: fb/fbpict.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbpixmap.c.o: fb/fbpixmap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbpoint.c.o: fb/fbpoint.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbpush.c.o: fb/fbpush.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbscreen.c.o: fb/fbscreen.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbseg.c.o: fb/fbseg.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbsetsp.c.o: fb/fbsetsp.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbsolid.c.o: fb/fbsolid.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbtrap.c.o: fb/fbtrap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbutil.c.o: fb/fbutil.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_fb__fb_fbwindow.c.o: fb/fbwindow.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_miarc.c.o: mi/miarc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mibitblt.c.o: mi/mibitblt.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_micmap.c.o: mi/micmap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_micopy.c.o: mi/micopy.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_midash.c.o: mi/midash.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_midispcur.c.o: mi/midispcur.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mieq.c.o: mi/mieq.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_miexpose.c.o: mi/miexpose.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mifillarc.c.o: mi/mifillarc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mifillrct.c.o: mi/mifillrct.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_migc.c.o: mi/migc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_miglblt.c.o: mi/miglblt.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mioverlay.c.o: mi/mioverlay.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mipointer.c.o: mi/mipointer.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mipoly.c.o: mi/mipoly.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mipolypnt.c.o: mi/mipolypnt.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mipolyrect.c.o: mi/mipolyrect.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mipolyseg.c.o: mi/mipolyseg.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mipolytext.c.o: mi/mipolytext.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mipushpxl.c.o: mi/mipushpxl.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_miscrinit.c.o: mi/miscrinit.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_misprite.c.o: mi/misprite.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mivaltree.c.o: mi/mivaltree.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_miwideline.c.o: mi/miwideline.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_miwindow.c.o: mi/miwindow.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mizerarc.c.o: mi/mizerarc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mizerclip.c.o: mi/mizerclip.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_mi__mi_mizerline.c.o: mi/mizerline.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_atom.c.o: dix/atom.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_colormap.c.o: dix/colormap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_cursor.c.o: dix/cursor.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_devices.c.o: dix/devices.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_dispatch.c.o: dix/dispatch.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_dixfonts.c.o: dix/dixfonts.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_main.c.o: dix/main.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_dixutils.c.o: dix/dixutils.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_enterleave.c.o: dix/enterleave.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_events.c.o: dix/events.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_eventconvert.c.o: dix/eventconvert.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_extension.c.o: dix/extension.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_gc.c.o: dix/gc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_gestures.c.o: dix/gestures.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_getevents.c.o: dix/getevents.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_globals.c.o: dix/globals.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_glyphcurs.c.o: dix/glyphcurs.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_grabs.c.o: dix/grabs.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_initatoms.c.o: dix/initatoms.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_inpututils.c.o: dix/inpututils.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_pixmap.c.o: dix/pixmap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_privates.c.o: dix/privates.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_property.c.o: dix/property.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_ptrveloc.c.o: dix/ptrveloc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_region.c.o: dix/region.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_registry.c.o: dix/registry.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_resource.c.o: dix/resource.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_selection.c.o: dix/selection.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_swaprep.c.o: dix/swaprep.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_swapreq.c.o: dix/swapreq.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_tables.c.o: dix/tables.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_touch.c.o: dix/touch.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dix__dix_window.c.o: dix/window.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_composite__composite_compalloc.c.o: composite/compalloc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_composite__composite_compext.c.o: composite/compext.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_composite__composite_compinit.c.o: composite/compinit.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_composite__composite_compoverlay.c.o: composite/compoverlay.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_composite__composite_compwindow.c.o: composite/compwindow.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_damageext__damageext_damageext.c.o: damageext/damageext.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dbe__dbe_dbe.c.o: dbe/dbe.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_dbe__dbe_midbe.c.o: dbe/midbe.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_randr.c.o: randr/randr.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrcrtc.c.o: randr/rrcrtc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrdispatch.c.o: randr/rrdispatch.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrinfo.c.o: randr/rrinfo.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrlease.c.o: randr/rrlease.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrmode.c.o: randr/rrmode.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrmonitor.c.o: randr/rrmonitor.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rroutput.c.o: randr/rroutput.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrpointer.c.o: randr/rrpointer.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrproperty.c.o: randr/rrproperty.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrprovider.c.o: randr/rrprovider.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrproviderproperty.c.o: randr/rrproviderproperty.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrscreen.c.o: randr/rrscreen.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrsdispatch.c.o: randr/rrsdispatch.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrtransform.c.o: randr/rrtransform.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_randr__randr_rrxinerama.c.o: randr/rrxinerama.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_miext_damage__miext_damage_damage.c.o: miext/damage/damage.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_render__render_animcur.c.o: render/animcur.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_render__render_filter.c.o: render/filter.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_render__render_glyph.c.o: render/glyph.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_render__render_matrix.c.o: render/matrix.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_render__render_miindex.c.o: render/miindex.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_render__render_mipict.c.o: render/mipict.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_render__render_mirect.c.o: render/mirect.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_render__render_mitrap.c.o: render/mitrap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_render__render_mitri.c.o: render/mitri.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_render__render_picture.c.o: render/picture.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_render__render_render.c.o: render/render.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_present__present_present.c.o: present/present.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_present__present_present_event.c.o: present/present_event.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_present__present_present_execute.c.o: present/present_execute.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_present__present_present_fake.c.o: present/present_fake.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_present__present_present_fence.c.o: present/present_fence.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_present__present_present_notify.c.o: present/present_notify.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_present__present_present_request.c.o: present/present_request.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_present__present_present_scmd.c.o: present/present_scmd.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_present__present_present_screen.c.o: present/present_screen.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_present__present_present_vblank.c.o: present/present_vblank.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_bigreq.c.o: Xext/bigreq.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_geext.c.o: Xext/geext.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_shape.c.o: Xext/shape.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_sleepuntil.c.o: Xext/sleepuntil.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_sync.c.o: Xext/sync.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_xcmisc.c.o: Xext/xcmisc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_xtest.c.o: Xext/xtest.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_dpms.c.o: Xext/dpms.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_shm.c.o: Xext/shm.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_hashtable.c.o: Xext/hashtable.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_xres.c.o: Xext/xres.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_saver.c.o: Xext/saver.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_xace.c.o: Xext/xace.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_panoramiX.c.o: Xext/panoramiX.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_panoramiXprocs.c.o: Xext/panoramiXprocs.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_panoramiXSwap.c.o: Xext/panoramiXSwap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_xvmain.c.o: Xext/xvmain.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_xvdisp.c.o: Xext/xvdisp.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xext__Xext_xvmc.c.o: Xext/xvmc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_miext_sync__miext_sync_misync.c.o: miext/sync/misync.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_miext_sync__miext_sync_misyncfd.c.o: miext/sync/misyncfd.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xfixes__xfixes_cursor.c.o: xfixes/cursor.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xfixes__xfixes_disconnect.c.o: xfixes/disconnect.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xfixes__xfixes_region.c.o: xfixes/region.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xfixes__xfixes_saveset.c.o: xfixes/saveset.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xfixes__xfixes_select.c.o: xfixes/select.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xfixes__xfixes_xfixes.c.o: xfixes/xfixes.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_allowev.c.o: Xi/allowev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_chgdctl.c.o: Xi/chgdctl.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_chgfctl.c.o: Xi/chgfctl.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_chgkbd.c.o: Xi/chgkbd.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_chgkmap.c.o: Xi/chgkmap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_chgprop.c.o: Xi/chgprop.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_chgptr.c.o: Xi/chgptr.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_closedev.c.o: Xi/closedev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_devbell.c.o: Xi/devbell.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_exevents.c.o: Xi/exevents.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_extinit.c.o: Xi/extinit.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_getbmap.c.o: Xi/getbmap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_getdctl.c.o: Xi/getdctl.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_getfctl.c.o: Xi/getfctl.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_getfocus.c.o: Xi/getfocus.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_getkmap.c.o: Xi/getkmap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_getmmap.c.o: Xi/getmmap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_getprop.c.o: Xi/getprop.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_getselev.c.o: Xi/getselev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_getvers.c.o: Xi/getvers.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_grabdev.c.o: Xi/grabdev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_grabdevb.c.o: Xi/grabdevb.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_grabdevk.c.o: Xi/grabdevk.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_gtmotion.c.o: Xi/gtmotion.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_listdev.c.o: Xi/listdev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_opendev.c.o: Xi/opendev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_queryst.c.o: Xi/queryst.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_selectev.c.o: Xi/selectev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_sendexev.c.o: Xi/sendexev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_setbmap.c.o: Xi/setbmap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_setdval.c.o: Xi/setdval.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_setfocus.c.o: Xi/setfocus.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_setmmap.c.o: Xi/setmmap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_setmode.c.o: Xi/setmode.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_ungrdev.c.o: Xi/ungrdev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_ungrdevb.c.o: Xi/ungrdevb.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_ungrdevk.c.o: Xi/ungrdevk.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xiallowev.c.o: Xi/xiallowev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xibarriers.c.o: Xi/xibarriers.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xichangecursor.c.o: Xi/xichangecursor.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xichangehierarchy.c.o: Xi/xichangehierarchy.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xigetclientpointer.c.o: Xi/xigetclientpointer.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xigrabdev.c.o: Xi/xigrabdev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xipassivegrab.c.o: Xi/xipassivegrab.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xiproperty.c.o: Xi/xiproperty.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xiquerydevice.c.o: Xi/xiquerydevice.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xiquerypointer.c.o: Xi/xiquerypointer.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xiqueryversion.c.o: Xi/xiqueryversion.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xiselectev.c.o: Xi/xiselectev.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xisetclientpointer.c.o: Xi/xisetclientpointer.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xisetdevfocus.c.o: Xi/xisetdevfocus.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi__Xi_xiwarppointer.c.o: Xi/xiwarppointer.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_ddxBeep.c.o: xkb/ddxBeep.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_ddxCtrls.c.o: xkb/ddxCtrls.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_ddxLEDs.c.o: xkb/ddxLEDs.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_ddxLoad.c.o: xkb/ddxLoad.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_maprules.c.o: xkb/maprules.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkmread.c.o: xkb/xkmread.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkbtext.c.o: xkb/xkbtext.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkbfmisc.c.o: xkb/xkbfmisc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkbout.c.o: xkb/xkbout.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkb.c.o: xkb/xkb.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkbUtils.c.o: xkb/xkbUtils.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkbEvents.c.o: xkb/xkbEvents.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkbAccessX.c.o: xkb/xkbAccessX.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkbSwap.c.o: xkb/xkbSwap.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkbLEDs.c.o: xkb/xkbLEDs.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkbInit.c.o: xkb/xkbInit.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkbActions.c.o: xkb/xkbActions.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_xkbPrKeyEv.c.o: xkb/xkbPrKeyEv.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_XKBMisc.c.o: xkb/XKBMisc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_XKBAlloc.c.o: xkb/XKBAlloc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_XKBGAlloc.c.o: xkb/XKBGAlloc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb__xkb_XKBMAlloc.c.o: xkb/XKBMAlloc.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_record__record_record.c.o: record/record.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_record__record_set.c.o: record/set.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_WaitFor.c.o: os/WaitFor.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_access.c.o: os/access.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_auth.c.o: os/auth.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_backtrace.c.o: os/backtrace.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_client.c.o: os/client.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_connection.c.o: os/connection.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_inputthread.c.o: os/inputthread.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_io.c.o: os/io.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_mitauth.c.o: os/mitauth.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_oscolor.c.o: os/oscolor.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_osinit.c.o: os/osinit.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_ospoll.c.o: os/ospoll.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_utils.c.o: os/utils.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_xdmauth.c.o: os/xdmauth.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_xsha1.c.o: os/xsha1.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_xstrans.c.o: os/xstrans.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_xprintf.c.o: os/xprintf.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_log.c.o: os/log.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_os__os_busfault.c.o: os/busfault.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xlibc__os_timingsafe_memcmp.c.o: os/timingsafe_memcmp.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb_stubs__xkb_ddxKillSrv.c.o: xkb/ddxKillSrv.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb_stubs__xkb_ddxPrivate.c.o: xkb/ddxPrivate.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xkb_stubs__xkb_ddxVT.c.o: xkb/ddxVT.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xserver_xi_stubs__Xi_stubs.c.o: Xi/stubs.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xvfb__hw_vfb_InitInput.c.o: hw/vfb/InitInput.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xvfb__hw_vfb_InitOutput.c.o: hw/vfb/InitOutput.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/xvfb__mi_miinitext.c.o: mi/miinitext.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
