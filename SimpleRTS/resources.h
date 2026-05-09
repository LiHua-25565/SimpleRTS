#ifndef _RESOURCES_H_
#define _RESOURCES_H_

class Resource
{
public:
    Resource() = default;
    ~Resource() = default;

    // 一次性加全部资源
    void Add(int wood, int stone, int gold, int mmeat)
    {
        this->wood += wood;
        this->stone += stone;
        this->gold += gold;
        this->meat += meat;
    }

    // 判断是否足够支付
    bool CanAfford(const Resource& cost) const
    {
        return wood >= cost.wood
            && stone >= cost.stone
            && gold >= cost.gold
            && meat >= cost.meat;
    }

    // 扣除花费
    void Subtract(const Resource& cost)
    {
        wood -= cost.wood;
        stone -= cost.stone;
        gold -= cost.gold;
        meat -= cost.meat;
    }

    // 清零
    void Clear()
    {
        wood = 0;
        stone = 0;
        gold = 0;
        meat = 0;
    }

private:
    int wood = 0;
    int stone = 0;
    int gold = 0;
    int meat = 0;
};
#endif // !_RESOURCES_H_
