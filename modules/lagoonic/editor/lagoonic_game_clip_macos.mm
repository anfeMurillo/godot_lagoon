/**************************************************************************/
/*  lagoonic_game_clip_macos.mm                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             LAGOONIC ENGINE                            */
/**************************************************************************/

#include "lagoonic_game_clip.h"

#include "servers/display/display_server.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

static NSString *const LAGOONIC_CLIP_NAME = @"lagoonic-game-clip";

// The display server adds one CALayerHost per embedded game straight to the
// window view's layer and moves it every frame in view coordinates.
static bool _lagoonic_is_layer_host(CALayer *p_layer) {
	return [NSStringFromClass([p_layer class]) isEqualToString:@"CALayerHost"];
}

void lagoonic_clip_game_layer(int64_t p_window_id, const Rect2 &p_rect, float p_radius, bool p_enabled) {
	DisplayServer *ds = DisplayServer::get_singleton();
	if (ds == nullptr) {
		return;
	}
	NSView *view = (__bridge NSView *)(void *)ds->window_get_native_handle(DisplayServerEnums::WINDOW_VIEW, (DisplayServerEnums::WindowID)p_window_id);
	if (view == nil || view.layer == nil) {
		return;
	}
	CALayer *root = view.layer;
	CALayer *clip = nil;
	for (CALayer *l in root.sublayers) {
		if ([l.name isEqualToString:LAGOONIC_CLIP_NAME]) {
			clip = l;
		}
	}

	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	if (!p_enabled || p_rect.size.x <= 0 || p_rect.size.y <= 0) {
		if (clip != nil) {
			for (CALayer *l in [clip.sublayers copy]) {
				[l removeFromSuperlayer];
				[root addSublayer:l];
			}
			[clip removeFromSuperlayer];
		}
		[CATransaction commit];
		return;
	}
	if (clip == nil) {
		clip = [CALayer layer];
		clip.name = LAGOONIC_CLIP_NAME;
		clip.masksToBounds = YES;
		clip.anchorPoint = CGPointZero;
		[root addSublayer:clip];
	}
	for (CALayer *l in [root.sublayers copy]) {
		if (l != clip && _lagoonic_is_layer_host(l)) {
			[l removeFromSuperlayer];
			[clip addSublayer:l];
		}
	}

	// Window pixels -> view points (the view is not flipped: y grows upward).
	const CGFloat scale = MAX((CGFloat)ds->screen_get_max_scale(), 1.0);
	const CGFloat height = view.frame.size.height;
	const CGRect r = CGRectMake(p_rect.position.x / scale, height - (p_rect.position.y + p_rect.size.y) / scale, p_rect.size.x / scale, p_rect.size.y / scale);
	// Same origin for frame and bounds: the hosts keep the view coordinates
	// the display server gives them.
	if (!CGRectEqualToRect(clip.bounds, r) || !CGPointEqualToPoint(clip.position, r.origin)) {
		clip.position = r.origin;
		clip.bounds = r;
	}
	clip.cornerRadius = p_radius / scale;
	clip.maskedCorners = kCALayerMinXMinYCorner | kCALayerMaxXMinYCorner; // Bottom corners.
	[CATransaction commit];
}
