#include "cli.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 长选项专用返回码（避开短选项字符） */
enum {
    OPT_SEED = 1000,
    OPT_CSV,
    OPT_SELFTEST
};

/* 取 argv[0] 的文件名部分，用于错误信息与用法提示 */
static const char* base_name(const char* path)
{
    const char* slash;

    if (path == NULL || path[0] == '\0') {
        return "puzzle";
    }
    slash = strrchr(path, '/');
    return (slash != NULL) ? slash + 1 : path;
}

static void set_defaults(Config* config)
{
    static const int default_target[3][3] = {
        {1, 2, 3},
        {4, 5, 6},
        {7, 8, 0}
    };

    memset(config, 0, sizeof(*config));
    memcpy(config->target, default_target, sizeof(default_target));
    config->method = METHOD_H2;
    config->repeat = 1;
    config->seed = CLI_DEFAULT_SEED;
    config->csv_path = NULL;
}

/* 统一的错误输出：<prog>: <原因> + 用法提示 */
static CliStatus fail(const char* prog, const char* fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: ", prog);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n运行 '%s --help' 查看用法。\n", prog);
    return CLI_RC_ERROR;
}

static CliStatus fail_state(const char* prog, const char* what, StateParseStatus status)
{
    const char* reason;

    switch (status) {
    case STATE_ERR_NOT_NUMBER:
        reason = "只能包含整数与空白分隔符";
        break;
    case STATE_ERR_COUNT:
        reason = "必须恰好包含 9 个整数";
        break;
    case STATE_ERR_RANGE:
        reason = "每个整数必须在 0..8 之间";
        break;
    case STATE_ERR_DUPLICATE:
        reason = "9 个整数必须是 0..8 各一次，不能重复";
        break;
    default:
        reason = "格式非法";
        break;
    }
    return fail(prog, "%s: %s", what, reason);
}

/* 解析正整数（>= 1 且不超过 INT_MAX），不做任何输出 */
static int parse_positive_int(const char* str, int* out)
{
    char* end = NULL;
    long value;

    if (str == NULL || out == NULL) {
        return 0;
    }
    errno = 0;
    value = strtol(str, &end, 10);
    if (end == str || *end != '\0' || errno == ERANGE || value < 1 || value > INT_MAX) {
        return 0;
    }
    *out = (int)value;
    return 1;
}

/* 解析 uint32 随机种子：不接受负号，范围 0..4294967295 */
static int parse_seed(const char* str, unsigned int* out)
{
    const char* p;
    char* end = NULL;
    unsigned long value;

    if (str == NULL || out == NULL) {
        return 0;
    }
    for (p = str; *p == ' ' || *p == '\t'; p++) {
        /* 跳过前导空白后再判断符号 */
    }
    if (*p == '-') {
        return 0;
    }
    errno = 0;
    value = strtoul(p, &end, 10);
    if (end == p || *end != '\0' || errno == ERANGE || value > 0xFFFFFFFFul) {
        return 0;
    }
    *out = (unsigned int)value;
    return 1;
}

/* 解析策略名（区分大小写，与文档一致） */
static int parse_method(const char* str, Method* out)
{
    if (str == NULL || out == NULL) {
        return 0;
    }
    if (strcmp(str, "h1") == 0) {
        *out = METHOD_H1;
    } else if (strcmp(str, "h2") == 0) {
        *out = METHOD_H2;
    } else if (strcmp(str, "bfs") == 0) {
        *out = METHOD_BFS;
    } else if (strcmp(str, "all") == 0) {
        *out = METHOD_ALL;
    } else {
        return 0;
    }
    return 1;
}

StateParseStatus parse_state_string(const char* str, int state[3][3])
{
    const char* p;
    int values[9];
    unsigned int seen = 0;
    int count = 0;
    int i;

    if (str == NULL || state == NULL) {
        return STATE_ERR_COUNT;
    }

    p = str;
    while (*p != '\0') {
        char* end = NULL;
        long value;

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (count >= 9) {
            return STATE_ERR_COUNT;
        }
        errno = 0;
        value = strtol(p, &end, 10);
        if (end == p) {
            return STATE_ERR_NOT_NUMBER;
        }
        if (errno == ERANGE || value < 0 || value > 8) {
            return STATE_ERR_RANGE;
        }
        values[count++] = (int)value;
        p = end;
    }

    if (count != 9) {
        return STATE_ERR_COUNT;
    }

    for (i = 0; i < 9; i++) {
        unsigned int bit = 1u << (unsigned int)values[i];

        if ((seen & bit) != 0u) {
            return STATE_ERR_DUPLICATE;
        }
        seen |= bit;
    }

    for (i = 0; i < 9; i++) {
        state[i / 3][i % 3] = values[i];
    }
    return STATE_OK;
}

CliStatus parse_cli_args(int argc, char* argv[], Config* config)
{
    static const struct option long_options[] = {
        {"start",     required_argument, NULL, 's'},
        {"target",    required_argument, NULL, 't'},
        {"method",    required_argument, NULL, 'm'},
        {"print-path", no_argument,      NULL, 'p'},
        {"repeat",    required_argument, NULL, 'r'},
        {"benchmark", required_argument, NULL, 'b'},
        {"seed",      required_argument, NULL, OPT_SEED},
        {"csv",       required_argument, NULL, OPT_CSV},
        {"selftest",  no_argument,       NULL, OPT_SELFTEST},
        {"help",      no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    const char* prog;
    int ch;

    if (config == NULL) {
        return CLI_RC_ERROR;
    }
    prog = (argc > 0 && argv != NULL) ? base_name(argv[0]) : "puzzle";
    set_defaults(config);

    /* optind = 0 让 glibc 完整重置扫描状态，允许同一进程内反复调用（单元测试）。 */
    optind = 0;
    opterr = 0; /* 错误信息由本模块统一输出 */

    while ((ch = getopt_long(argc, argv, ":s:t:m:pr:b:h", long_options, NULL)) != -1) {
        switch (ch) {
        case 's': {
            StateParseStatus status = parse_state_string(optarg, config->start);
            if (status != STATE_OK) {
                return fail_state(prog, "初始状态", status);
            }
            config->has_start = 1;
            break;
        }
        case 't': {
            StateParseStatus status = parse_state_string(optarg, config->target);
            if (status != STATE_OK) {
                return fail_state(prog, "目标状态", status);
            }
            break;
        }
        case 'm':
            if (!parse_method(optarg, &config->method)) {
                return fail(prog, "未知的求解策略 '%s'（可选：h1、h2、bfs、all）", optarg);
            }
            break;
        case 'p':
            config->print_path = 1;
            break;
        case 'r':
            if (!parse_positive_int(optarg, &config->repeat)) {
                return fail(prog, "重复次数必须是正整数，实际为 '%s'", optarg);
            }
            break;
        case 'b':
            if (!parse_positive_int(optarg, &config->benchmark)) {
                return fail(prog, "批量规模必须是正整数，实际为 '%s'", optarg);
            }
            break;
        case OPT_SEED:
            if (!parse_seed(optarg, &config->seed)) {
                return fail(prog, "随机种子必须是 0..4294967295 之间的整数，实际为 '%s'", optarg);
            }
            break;
        case OPT_CSV:
            config->csv_path = optarg;
            break;
        case OPT_SELFTEST:
            config->selftest = 1;
            break;
        case 'h':
            return CLI_RC_HELP;
        case ':':
            return fail(prog, "选项缺少参数");
        case '?':
        default:
            if (optind > 0 && optind <= argc && argv[optind - 1] != NULL) {
                return fail(prog, "无法识别的选项 '%s'", argv[optind - 1]);
            }
            return fail(prog, "无法识别的选项");
        }
    }

    if (optind < argc) {
        return fail(prog, "多余的位置参数 '%s'，本程序不接受位置参数", argv[optind]);
    }
    /* -s 在自检与批量基准模式下不需要（后者随机生成可解初态） */
    if (!config->selftest && config->benchmark <= 0 && !config->has_start) {
        return fail(prog, "缺少必填参数 -s/--start（或使用 --selftest / -b）");
    }
    return CLI_RC_OK;
}

void print_usage(const char* prog_name)
{
    const char* prog = base_name(prog_name);

    printf("用法: %s -s \"<9 个数字>\" [选项]\n", prog);
    printf("\n");
    printf("8 数码问题 A* 求解程序：对比 h1 / h2 / BFS 三种搜索策略。\n");
    printf("\n");
    printf("选项:\n");
    printf("  -s, --start <string>     初始状态，9 个空格分隔的数字（必填，--selftest 除外）\n");
    printf("  -t, --target <string>    目标状态，默认 \"%s\"\n", CLI_DEFAULT_TARGET);
    printf("  -m, --method <name>      求解策略：h1、h2、bfs、all（默认 h2）\n");
    printf("  -p, --print-path         打印完整移动步骤与棋盘矩阵\n");
    printf("  -r, --repeat <N>         重复搜索 N 次并输出中位数，默认 1\n");
    printf("  -b, --benchmark <N>      随机生成 N 个可解初态批量运行三种策略\n");
    printf("      --seed <uint32>      批量模式随机种子，默认 %u\n", CLI_DEFAULT_SEED);
    printf("      --csv <file>         将指标写入 CSV 文件\n");
    printf("      --selftest           运行内建断言自检\n");
    printf("  -h, --help               显示本帮助\n");
    printf("\n");
    printf("退出码: 0 成功; 1 参数或输入非法; 2 无解; 3 内存不足/超出节点上限; 4 自检失败\n");
    printf("\n");
    printf("示例:\n");
    printf("  %s -s \"1 2 3 4 0 6 7 5 8\" -m all\n", prog);
    printf("  %s -s \"1 2 3 4 0 6 7 5 8\" -m h2 -p\n", prog);
    printf("  %s -b 50 --seed 20261020 --csv results.csv\n", prog);
    printf("  %s --selftest\n", prog);
}
