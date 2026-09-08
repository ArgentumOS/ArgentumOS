/* argentum/control.cpp — S2.2b Control base (docs/design/
 * argentum-s22-control-first-leaves.md). Enabled + std::function
 * action + the chrome state machine. The event plumbing that drives
 * hover/armed/focused lands with Button (S2.2c); the accessors are
 * here so subclasses and the event loop can use them. */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

namespace argentum {

Control::Control()
	: ctrl_(new Impl())
{
}

Control::~Control()
{
	delete ctrl_;
}

void
Control::setEnabled(bool enabled)
{
	if (ctrl_->enabled != enabled) {
		ctrl_->enabled = enabled;
		/* a11y mirrors control state: a disabled control is not
		 * operable, so it reports disabled */
		setAccessibilityEnabled(enabled);
		setNeedsDisplay();
	}
}

bool
Control::isEnabled() const
{
	return ctrl_->enabled;
}

void
Control::setAction(Action action)
{
	ctrl_->action = std::move(action);
}

void
Control::sendAction()
{
	if (ctrl_->enabled && ctrl_->action) {
		ctrl_->action(this);
	}
}

bool
Control::acceptsFirstResponder() const
{
	return true;
}

void
Control::becomeFirstResponder()
{
	setFocused(true);
}

void
Control::resignFirstResponder()
{
	setFocused(false);
}

ControlState
Control::state() const
{
	if (!ctrl_->enabled) {
		return ControlState::Disabled;
	}
	if (ctrl_->armed) {
		return ControlState::Armed;
	}
	if (ctrl_->hovered) {
		return ControlState::Hover;
	}
	if (ctrl_->focused) {
		return ControlState::Focused;
	}
	return ControlState::Idle;
}

void
Control::setHovered(bool hovered)
{
	if (ctrl_->hovered != hovered) {
		ctrl_->hovered = hovered;
		setNeedsDisplay();
	}
}

void
Control::setArmed(bool armed)
{
	if (ctrl_->armed != armed) {
		ctrl_->armed = armed;
		setNeedsDisplay();
	}
}

void
Control::setFocused(bool focused)
{
	if (ctrl_->focused != focused) {
		ctrl_->focused = focused;
		setNeedsDisplay();
	}
}

bool
Control::hovered() const
{
	return ctrl_->hovered;
}

bool
Control::armed() const
{
	return ctrl_->armed;
}

bool
Control::focused() const
{
	return ctrl_->focused;
}

} /* namespace argentum */
