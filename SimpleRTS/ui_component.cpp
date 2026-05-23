#include "ui_component.h"

void UIComponent::set_anchor(UIAnchor anchor) { anchor = anchor; }
void UIComponent::set_offset(float offX, float offY) { offX = offX; offY = offY; }
void UIComponent::set_size(float w, float h) { w = w; h = h; }
void UIComponent::set_size_percent(float w_percent, float h_percent) {
    w_percent = w_percent; h_percent = h_percent;
}

const UIRect& UIComponent::get_rect() const { return rect; }

void UIComponent::ComputeAnchorPosition(UIAnchor anchor, const UIRect& parent,
    float offX, float offY, float& outX, float& outY) const {
    switch (anchor) {
    case UIAnchor::TopLeft:
        outX = parent.x + offX;
        outY = parent.y + offY;
        break;
    case UIAnchor::TopCenter:
        outX = parent.x + parent.w * 0.5f + offX;
        outY = parent.y + offY;
        break;
    case UIAnchor::TopRight:
        outX = parent.x + parent.w + offX;
        outY = parent.y + offY;
        break;
    case UIAnchor::MiddleLeft:
        outX = parent.x + offX;
        outY = parent.y + parent.h * 0.5f + offY;
        break;
    case UIAnchor::MiddleCenter:
        outX = parent.x + parent.w * 0.5f + offX;
        outY = parent.y + parent.h * 0.5f + offY;
        break;
    case UIAnchor::MiddleRight:
        outX = parent.x + parent.w + offX;
        outY = parent.y + parent.h * 0.5f + offY;
        break;
    case UIAnchor::BottomLeft:
        outX = parent.x + offX;
        outY = parent.y + parent.h + offY;
        break;
    case UIAnchor::BottomCenter:
        outX = parent.x + parent.w * 0.5f + offX;
        outY = parent.y + parent.h + offY;
        break;
    case UIAnchor::BottomRight:
        outX = parent.x + parent.w + offX;
        outY = parent.y + parent.h + offY;
        break;
    }
}

void UIComponent::compute_layout(const UIRect& parent_rect) {
    float px, py;
    ComputeAnchorPosition(anchor, parent_rect, offX, offY, px, py);
    float fw = (w_percent > 0) ? parent_rect.w * w_percent : w;
    float fh = (h_percent > 0) ? parent_rect.h * h_percent : h;
    rect = { px, py, fw, fh };

    for (auto& child : children) {
        child->compute_layout(rect);
    }
}

void UIComponent::on_update(float delta) {
    for (auto& child : children) child->on_update(delta);
}

void UIComponent::render(SDL_Renderer* renderer) {
    for (auto& child : children) child->render(renderer);
}

bool UIComponent::on_mouse_down(float x, float y) {
    // 可扩展：检查点击是否在 rect_ 内，并分发给子组件
    for (auto& child : children) {
        if (child->on_mouse_down(x, y)) return true;
    }
    return false;
}

bool UIComponent::on_mouse_up(float x, float y) {
    for (auto& child : children) {
        if (child->on_mouse_up(x, y)) return true;
    }
    return false;
}
