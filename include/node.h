#ifndef NODE_H
#define NODE_H

/*
 * 搜索树节点定义，由搜索相关的各模块共享。
 *
 * 所有权：Node 一律由内存池（pool 模块）分配与释放；小顶堆与状态表只持有指针，
 * 不得 free 节点。这样可以从结构上排除泄漏与 double free。
 */

typedef struct Node {
    int  state[3][3];       /* 3x3 棋盘格局（0 表示空格） */
    unsigned int code;      /* state 的 base-9 编码缓存，恒等于 state_encode(state) */
    int  zero_x;            /* 空格行坐标（0..2） */
    int  zero_y;            /* 空格列坐标（0..2） */
    int  g;                 /* 起点到当前节点的实际代价（步数） */
    int  h;                 /* 到目标的估计代价 */
    int  f;                 /* f = g + h */
    struct Node* parent;    /* 父节点指针，用于回溯解路径 */
} Node;

#endif /* NODE_H */
