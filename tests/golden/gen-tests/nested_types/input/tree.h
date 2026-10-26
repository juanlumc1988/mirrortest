class Tree {
public:
    struct Node { int key; };
    enum class Order { Pre, In, Post };
    Node root() const;
    void walk(Order order, const Node& from);
};
