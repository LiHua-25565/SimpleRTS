#ifndef _UI_COMPONENT_H_
#define _UI_COMPONENT_H_

#include <SDL3/SDL.h>
#include <vector>
#include <memory>
#include <functional>
#include <string>

enum class UIAnchor {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, MiddleCenter, MiddleRight,
    BottomLeft, BottomCenter, BottomRight
};

struct UIRect {
    float x, y, w, h;
};

class UIComponent {
public:
    UIComponent() = default;
    virtual ~UIComponent() = default;

    void set_anchor(UIAnchor anchor);
    void set_offset(float offX, float offY);
    void set_size(float w, float h);
    void set_size_percent(float w_percent, float h_percent);
    const UIRect& get_rect() const;

    // 模板成员函数必须保留在头文件中
    template<typename T, typename... Args>
    T* add_child(Args&&... args) {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = child.get();
        children.push_back(std::move(child));
        return ptr;
    }

    virtual void compute_layout(const UIRect& parent_rect);
    virtual void on_update(float delta);
    virtual void render(SDL_Renderer* renderer);
    virtual bool on_mouse_down(float x, float y);
    virtual bool on_mouse_up(float x, float y);

protected:
    void ComputeAnchorPosition(UIAnchor anchor, const UIRect& parent,
        float offX, float offY, float& outX, float& outY) const;

    UIRect rect{};
    std::vector<std::unique_ptr<UIComponent>> children;

private:
    UIAnchor anchor = UIAnchor::TopLeft;
    float offX = 0, offY = 0;
    float w = 0, h = 0;
    float w_percent = 0, h_percent = 0;
};

#endif // !_UI_COMPONENT_H_

