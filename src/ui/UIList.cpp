// UIList accessors. Addresses 0x23f838..0x23fff4.
//
// The structural finding here is that UIList holds two subobjects and forwards
// almost everything to them: a ListState at +0x150 that owns scroll position and
// speed, and a ListDisplay at +0x1c8 that owns geometry. Every forwarder is the
// same six instructions with the constant offset in the delay slot, which is
// what pins the two offsets beyond doubt.

#include "gh2/inferred_types.h"

// --- forwarded to ListState (+0x150) ---------------------------------------

int UIList::Selected() const { return mState.Selected(); }               // 0x23f8c8
int UIList::SelectedDisplay() const { return mState.SelectedDisplay(); } // 0x23f8e8
bool UIList::IsScrolling() const { return mState.IsScrolling(); }        // 0x23f9c8
float UIList::Speed() const { return mState.Speed(); }                   // 0x23f888
void UIList::SetSpeed(float speed) { mState.SetSpeed(speed); }           // 0x23fb68

// --- forwarded to ListDisplay (+0x1c8) -------------------------------------

float UIList::Spacing() const { return mDisplay.Spacing(); }             // 0x23f838
float UIList::ArrowOffset() const { return mDisplay.ArrowOffset(); }     // 0x23f858
int UIList::FadeOffset() const { return mDisplay.FadeOffset(); }         // 0x23f8a8
void UIList::SetArrowOffset(float o) { mDisplay.SetArrowOffset(o); }     // 0x23fab0
void UIList::SetFadeOffset(int o) { mDisplay.SetFadeOffset(o); }         // 0x23fb88

// --- own state -------------------------------------------------------------

bool UIList::IsCircular() const { return mCircular != 0; } // 0x23f880
int UIList::NumDisplay() const { return mNumDisplay; }     // 0x23f878

// addiu $v0, $zero, 0x64. A hard 100, not a member read. This is the base
// class default that a data-backed subclass overrides.
int UIList::NumData() const { return 100; } // 0x23fff0

void UIList::Enter() {} // 0x23fdf0
void UIList::Exit() {}  // 0x23fdf8
