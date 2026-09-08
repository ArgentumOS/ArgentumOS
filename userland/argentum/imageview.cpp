/* argentum/imageview.cpp — S2.3c ImageView (docs/design/
 * argentum-s23-tier1-rest.md): an input-free View that draws a
 * view-supplied BitmapImage into its frame with a content mode
 * (Center / ScaleToFit / Stretch — see GraphicsContext::drawImage).
 * A11y role Image. Image-file decoding (PNG/JPEG) is a later media
 * milestone; v1 images are painted by the app through a
 * GraphicsContext on a BitmapImage. */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

namespace argentum {

ImageView::ImageView()
	: iv_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::Image);
}

ImageView::~ImageView()
{
	delete iv_;
}

void
ImageView::setImage(BitmapImage *image)
{
	if (iv_->image != image) {
		iv_->image = image;
		setNeedsDisplay();
	}
}

BitmapImage *
ImageView::image() const
{
	return iv_->image;
}

void
ImageView::setContentMode(ImageContentMode mode)
{
	if (iv_->mode != mode) {
		iv_->mode = mode;
		setNeedsDisplay();
	}
}

ImageContentMode
ImageView::contentMode() const
{
	return iv_->mode;
}

void
ImageView::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);

	if (!iv_->image) {
		/* placeholder: a thin page-coloured panel so the frame
		 * is visible before an image is attached */
		g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h, 2,
				  theme.chromeOutline());
		g.fillRoundedRect(1, 1, (unsigned) (w - 2),
				  (unsigned) (h - 2), 1,
				  theme.page());
		return;
	}
	g.drawImage(*iv_->image, 0, 0, (unsigned) w, (unsigned) h,
		    iv_->mode);
}

} /* namespace argentum */
