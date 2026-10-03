#ifndef CLI_H
#define CLI_H

/*
 * 命令行解析模块。
 *
 * 职责：解析终端参数（getopt_long），完成状态字符串到 3x3 数组的转换与合法性校验，
 * 并把结果写入 Config。本模块不参与搜索与输出（--help 的文本除外）。
 */

/* 默认目标状态：1 2 3 4 5 6 7 8 0 */
#define CLI_DEFAULT_TARGET "1 2 3 4 5 6 7 8 0"

/* 批量模式默认随机种子（固定值，保证同一 seed 下结果可复现） */
#define CLI_DEFAULT_SEED 20261020u

/* 求解策略 */
typedef enum {
    METHOD_H1 = 0, /* 放错位置数码数 */
    METHOD_H2,     /* 曼哈顿距离（默认） */
    METHOD_BFS,    /* h = 0 */
    METHOD_ALL     /* 依次运行三种策略并输出对比表 */
} Method;

/* parse_state_string 的返回值 */
typedef enum {
    STATE_OK = 0,
    STATE_ERR_NOT_NUMBER, /* 含非数字记号 */
    STATE_ERR_COUNT,      /* 数字个数不是 9 */
    STATE_ERR_RANGE,      /* 数字超出 0..8 */
    STATE_ERR_DUPLICATE   /* 9 个数字中出现重复 */
} StateParseStatus;

/* parse_cli_args 的返回值 */
typedef enum {
    CLI_RC_OK = 0, /* 解析成功 */
    CLI_RC_HELP,   /* 用户请求 -h/--help，非错误 */
    CLI_RC_ERROR   /* 用法或输入非法，原因已输出到 stderr */
} CliStatus;

typedef struct {
    int  start[3][3];      /* 初始状态（-s） */
    int  target[3][3];     /* 目标状态（-t），默认 CLI_DEFAULT_TARGET */
    int  has_start;        /* 是否提供 -s */
    Method method;         /* -m，默认 METHOD_H2 */
    int  print_path;       /* -p */
    int  repeat;           /* -r，默认 1 */
    int  benchmark;        /* -b，>0 表示批量模式 */
    unsigned int seed;     /* --seed，默认 CLI_DEFAULT_SEED */
    const char* csv_path;  /* --csv，可为 NULL */
    int  selftest;         /* --selftest */
} Config;

/*
 * 解析命令行参数并填充 config（先写入默认值，再被选项覆盖）。
 * 返回值见 CliStatus；返回 CLI_RC_ERROR 时已向 stderr 输出原因与用法提示。
 * 允许在同一进程内被反复调用（内部会重置 getopt 状态）。
 */
CliStatus parse_cli_args(int argc, char* argv[], Config* config);

/*
 * 解析由空白分隔的 9 个数字，成功时写入 state 并返回 STATE_OK。
 * 返回值见 StateParseStatus；本函数不输出任何信息。
 */
StateParseStatus parse_state_string(const char* str, int state[3][3]);

/* 向 stdout 打印帮助与示例。prog_name 可为 argv[0]。 */
void print_usage(const char* prog_name);

#endif /* CLI_H */
