#define WIN32_LEAN_AND_MEAN /* 减少 Windows 头文件内容。 */
#include <windows.h> /* 使用目录创建和文件原子替换。 */
#include <stdio.h> /* 使用基础文本文件读写。 */
#include <string.h> /* 使用字符串比较和复制。 */
#include <errno.h> /* 区分首次运行与文件无法访问。 */
#include <time.h> /* 保存本局完成时间。 */
#include "game.h" /* 引入账号、成绩和设置结构。 */

static char dataFolder[128] = "data"; /* 默认将存档保存在程序旁的 data 文件夹。 */

static int fail(char *error, const char *message) /* 统一写入错误信息并返回失败。 */
{ /* 函数开始。 */
    snprintf(error, MESSAGE_SIZE, "%s", message); /* 限制错误提示长度。 */
    return 0; /* 返回失败。 */
} /* 函数结束。 */

void storage_folder(const char *folder) /* 测试使用独立目录，不触碰正式账号数据。 */
{ /* 函数开始。 */
    snprintf(dataFolder, sizeof(dataFolder), "%s", folder); /* 保存调用方指定的目录。 */
} /* 函数结束。 */

static void file_path(char path[256], const char *name) /* 拼接固定数据文件名。 */
{ /* 函数开始。 */
    snprintf(path, 256, "%s/%s", dataFolder, name); /* 所有文件均位于第二版自己的目录下。 */
} /* 函数结束。 */

static FILE *open_write(const char *name, char *error) /* 准备同目录临时文件。 */
{ /* 函数开始。 */
    char path[256]; /* 保存临时文件路径。 */
    FILE *file; /* 保存文件指针。 */
    if (!CreateDirectoryA(dataFolder, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) /* 首次保存创建目录。 */
    { /* 创建失败分支开始。 */
        fail(error, "无法创建存档目录。"); /* 提示目录错误。 */
        return NULL; /* 不尝试在其他地方保存。 */
    } /* 创建失败分支结束。 */
    file_path(path, name); /* 得到临时文件的完整相对路径。 */
    file = fopen(path, "wb"); /* 创建临时文件，原正式文件保持不变。 */
    if (file == NULL) /* 写入权限不足或路径无效。 */
        fail(error, "无法创建存档临时文件。"); /* 提示保存失败。 */
    return file; /* 返回文件指针或空指针。 */
} /* 函数结束。 */

static int finish_write(FILE *file, const char *temporary, const char *name, char *error) /* 提交完整文件。 */
{ /* 函数开始。 */
    char source[256]; /* 临时文件路径。 */
    char target[256]; /* 正式文件路径。 */
    int okay = !ferror(file); /* 先记录此前 fprintf 是否失败。 */
    if (fflush(file) != 0) /* 将标准库缓冲区写入操作系统。 */
        okay = 0; /* 标记未能完整写出。 */
    if (fclose(file) != 0) /* 关闭时仍可能遇到写入错误。 */
        okay = 0; /* 标记文件未能正常关闭。 */
    file_path(source, temporary); /* 取得本函数创建的临时文件路径。 */
    file_path(target, name); /* 取得需要替换的正式文件路径。 */
    if (okay) /* 只有完整写入后才替换正式文件。 */
        okay = MoveFileExA(source, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0; /* 提交新文件。 */
    if (!okay) /* 保存失败时尽力清理自己的临时文件。 */
    { /* 保存失败分支开始。 */
        remove(source); /* 只删除本次临时文件，保留旧正式文件。 */
        return fail(error, "文件保存失败，原存档未被替换。"); /* 提示用户没有保存成功。 */
    } /* 保存失败分支结束。 */
    error[0] = 0; /* 清除旧错误。 */
    return 1; /* 正式保存完成。 */
} /* 函数结束。 */

static void trim_newline(char *line) /* 去除文本行末的回车和换行。 */
{ /* 函数开始。 */
    line[strcspn(line, "\r\n")] = 0; /* 不去除账号或密码中的普通空格。 */
} /* 函数结束。 */

static int accounts_load(Account accounts[MAX_ACCOUNTS], char *error) /* 返回账号数量或负一。 */
{ /* 函数开始。 */
    char path[256]; /* 账号文件路径。 */
    char line[400]; /* 每行最多保存一个账号及两段摘要文本。 */
    FILE *file; /* 打开的账号文件。 */
    int count = 0; /* 已读取的账号数量。 */
    int okay = 1; /* 文件是否合法。 */
    file_path(path, "accounts.txt"); /* 账号文件与旧版本独立。 */
    file = fopen(path, "rb"); /* 只读打开账号文件。 */
    if (file == NULL) /* 首次运行允许没有账号文件。 */
    { /* 打开失败分支开始。 */
        if (errno == ENOENT) /* 确实不存在文件时视为空账号表。 */
            return 0; /* 返回零个账号。 */
        fail(error, "账号文件无法读取。"); /* 权限或磁盘错误不能当作空文件。 */
        return -1; /* 保留原文件，不继续注册覆盖。 */
    } /* 打开失败分支结束。 */
    if (fgets(line, sizeof(line), file) == NULL) /* 文件必须具有格式头。 */
        okay = 0; /* 拒绝空的损坏文件。 */
    else /* 检查格式版本。 */
    { /* 格式头分支开始。 */
        trim_newline(line); /* 同时允许 Windows 和 Unix 换行。 */
        okay = strcmp(line, "NOTE_COLLISION_V2_ACCOUNTS 1") == 0; /* 只接受本版本格式。 */
    } /* 格式头分支结束。 */
    while (okay && fgets(line, sizeof(line), file) != NULL) /* 按行读取账号。 */
    { /* 账号读取循环开始。 */
        Account item = {0}; /* 临时账号结构清零。 */
        int used = 0; /* 记录解析结束的位置。 */
        int fields; /* 记录实际解析的字段数量。 */
        int i; /* 检查是否重复账号。 */
        trim_newline(line); /* 去除行末换行。 */
        fields = sscanf(line, "%64[^\t]\t%32[0-9a-f]\t%64[0-9a-f]%n", item.name, item.salt, item.hash, &used); /* 按制表符解析。 */
        if (fields != 3 || line[used] != 0 || strlen(item.salt) != 32 || strlen(item.hash) != 64) /* 校验字段及长度。 */
            okay = 0; /* 不接受不完整或多余内容。 */
        if (!text_valid(item.name, 16) || count >= MAX_ACCOUNTS) /* 检查账号编码与数组容量。 */
            okay = 0; /* 拒绝非法文件内容。 */
        for (i = 0; i < count; i++) /* 重复账号也视为损坏文件。 */
        { /* 查重循环开始。 */
            if (strcmp(accounts[i].name, item.name) == 0) /* 账号名采用区分大小写比较。 */
                okay = 0; /* 同一文件不能有重复账号。 */
        } /* 查重循环结束。 */
        if (okay) /* 只有当前记录通过校验才写入数组。 */
            accounts[count++] = item; /* 保存账号并增加数量。 */
    } /* 账号读取循环结束。 */
    if (ferror(file)) /* 检查底层读取错误。 */
        okay = 0; /* 不能将截断读取当作成功。 */
    fclose(file); /* 关闭只读文件。 */
    if (!okay) /* 任意记录无效都拒绝整份账号表。 */
    { /* 文件无效分支开始。 */
        fail(error, "账号文件格式错误，已保留原文件。"); /* 输出明确错误。 */
        return -1; /* 不允许后续覆盖损坏账号文件。 */
    } /* 文件无效分支结束。 */
    return count; /* 返回成功读取的数量。 */
} /* 函数结束。 */

static int account_input(const char *name, const char *password, char *error) /* 验证注册及登录输入。 */
{ /* 函数开始。 */
    size_t length = strlen(name); /* 用于检查账号首尾普通空格。 */
    if (!text_valid(name, 16) || !text_valid(password, 64)) /* 按 UTF-16 长度检查，支持中文。 */
        return fail(error, "账号需要 1~16 个字符，密码需要 1~64 个字符，不能包含控制字符。"); /* 输出格式要求。 */
    if (name[0] == ' ' || name[length - 1] == ' ') /* 不自动改变账号文本。 */
        return fail(error, "账号首尾不能有空格。"); /* 用户需要自行修正输入。 */
    return 1; /* 输入格式有效。 */
} /* 函数结束。 */

int account_register(const char *name, const char *password, char *error) /* 注册账号并保存加盐摘要。 */
{ /* 函数开始。 */
    Account accounts[MAX_ACCOUNTS]; /* 使用固定数组，不使用动态分配。 */
    Account item = {0}; /* 保存新账号。 */
    int count; /* 现有账号数量。 */
    int i; /* 遍历已有账号。 */
    FILE *file; /* 本次保存使用的临时文件。 */
    error[0] = 0; /* 清除上一次错误。 */
    if (!account_input(name, password, error)) /* 先验证输入格式。 */
        return 0; /* 不读取或改写文件。 */
    count = accounts_load(accounts, error); /* 读取并完整校验已有账号。 */
    if (count < 0) /* 读取失败时保留原文件。 */
        return 0; /* 返回失败。 */
    for (i = 0; i < count; i++) /* 检查账号名是否已经存在。 */
    { /* 查重循环开始。 */
        if (strcmp(accounts[i].name, name) == 0) /* 采用区分大小写的账号比较。 */
            return fail(error, "该账号已经存在。"); /* 不创建重复账号。 */
    } /* 查重循环结束。 */
    if (count == MAX_ACCOUNTS) /* 固定数组已满。 */
        return fail(error, "账号数量已达到一百个上限。"); /* 不写入越界位置。 */
    snprintf(item.name, sizeof(item.name), "%s", name); /* 保存经过校验的账号名。 */
    if (!make_salt(item.salt) || !password_hash(item.salt, password, item.hash)) /* 随机数或摘要失败。 */
        return fail(error, "系统无法生成账号密码摘要。"); /* 不退回明文保存。 */
    accounts[count++] = item; /* 将新账号放入数组。 */
    file = open_write("accounts.tmp", error); /* 准备同目录临时文件。 */
    if (file == NULL) /* 无法写入时禁止继续。 */
        return 0; /* 返回保存失败。 */
    fprintf(file, "NOTE_COLLISION_V2_ACCOUNTS 1\n"); /* 写入固定格式和版本。 */
    for (i = 0; i < count; i++) /* 按行保存所有账号。 */
        fprintf(file, "%s\t%s\t%s\n", accounts[i].name, accounts[i].salt, accounts[i].hash); /* 不写入明文密码。 */
    return finish_write(file, "accounts.tmp", "accounts.txt", error); /* 完整写出后替换正式文件。 */
} /* 函数结束。 */

int account_login(const char *name, const char *password, char *error) /* 只验证账号，不写回账号文件。 */
{ /* 函数开始。 */
    Account accounts[MAX_ACCOUNTS]; /* 保存从文件中读到的账号。 */
    int count; /* 读取到的数量。 */
    int i; /* 查找需要登录的账号。 */
    char hash[65]; /* 保存使用输入密码计算出的摘要。 */
    error[0] = 0; /* 清除旧提示。 */
    if (!account_input(name, password, error)) /* 检查输入长度和编码。 */
        return 0; /* 输入不合法时不继续。 */
    count = accounts_load(accounts, error); /* 读取账号。 */
    if (count < 0) /* 文件读取失败。 */
        return 0; /* 不隐藏文件错误。 */
    for (i = 0; i < count; i++) /* 查找完整匹配的账号名。 */
    { /* 查找循环开始。 */
        if (strcmp(accounts[i].name, name) != 0) /* 当前不是目标账号。 */
            continue; /* 继续查找。 */
        if (!password_hash(accounts[i].salt, password, hash)) /* 以已保存的盐重新计算。 */
            return fail(error, "密码校验接口调用失败。"); /* 系统错误不当作密码正确。 */
        if (strcmp(hash, accounts[i].hash) == 0) /* 两次摘要一致。 */
            return 1; /* 验证成功。 */
    } /* 查找循环结束。 */
    return fail(error, "账号或密码错误。"); /* 不区分账号不存在与密码错误。 */
} /* 函数结束。 */

static int record_valid(const Record *record) /* 检查成绩身份及原始统计。 */
{ /* 函数开始。 */
    if (!text_valid(record->name, 16)) /* 账号必须可显示且符合长度约束。 */
        return 0; /* 拒绝无效账号。 */
    if (record->song < 0 || record->song > 1) /* 检查歌曲下标。 */
        return 0; /* 拒绝未知歌曲。 */
    if (record->difficulty < 0 || record->difficulty > 2) /* 检查难度下标。 */
        return 0; /* 拒绝未知难度。 */
    return stats_valid(&record->stats, record->noteCount); /* 只有完整合法统计可以保存。 */
} /* 函数结束。 */

int records_load(Record records[MAX_RECORDS], char *error) /* 读取全部最佳成绩，出错返回负一。 */
{ /* 函数开始。 */
    char path[256]; /* 保存成绩文件路径。 */
    char line[500]; /* 保存一行成绩。 */
    FILE *file; /* 保存只读文件指针。 */
    int count = 0; /* 已读取条目数。 */
    int okay = 1; /* 整份文件是否合法。 */
    file_path(path, "scores.txt"); /* 与其他版本的成绩文件独立。 */
    file = fopen(path, "rb"); /* 打开只读文件。 */
    if (file == NULL) /* 首次运行允许没有成绩。 */
    { /* 打开失败分支开始。 */
        if (errno == ENOENT) /* 文件不存在意味着空榜。 */
            return 0; /* 返回零条记录。 */
        fail(error, "成绩文件无法读取。"); /* 其他错误需要明确报告。 */
        return -1; /* 不把无法读取的文件当作空榜。 */
    } /* 打开失败分支结束。 */
    if (fgets(line, sizeof(line), file) == NULL) /* 文件必须带有格式版本头。 */
        okay = 0; /* 空文件不合法。 */
    else /* 校验格式版本。 */
    { /* 格式头分支开始。 */
        trim_newline(line); /* 去除回车和换行。 */
        okay = strcmp(line, "NOTE_COLLISION_V2_SCORES 1") == 0; /* 只接受第二版格式。 */
    } /* 格式头分支结束。 */
    while (okay && fgets(line, sizeof(line), file) != NULL) /* 逐行读取最佳成绩。 */
    { /* 成绩读取循环开始。 */
        Record item = {0}; /* 初始化当前条目。 */
        int used = 0; /* 保存解析停止位置。 */
        int fields; /* 保存成功解析的字段数。 */
        int i; /* 检查是否存在重复最佳成绩键。 */
        trim_newline(line); /* 去除行末换行。 */
        fields = sscanf(line, "%64[^\t]\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%31[^\r\n]%n", item.name, &item.song, &item.difficulty, &item.noteCount, &item.stats.perfect, &item.stats.great, &item.stats.good, &item.stats.miss, &item.stats.maxCombo, item.date, &used); /* 读取各项原始统计。 */
        item.stats.finished = item.noteCount; /* 完整记录应覆盖全部音符。 */
        if (fields != 10 || line[used] != 0 || count >= MAX_RECORDS) /* 检查完整解析和数组容量。 */
            okay = 0; /* 拒绝不完整记录。 */
        if (item.noteCount < 1 || item.noteCount > MAX_NOTES) /* 先限制数量再进行乘法。 */
            okay = 0; /* 拒绝异常规模。 */
        if (item.stats.perfect < 0 || item.stats.perfect > MAX_NOTES) /* 防止计算权重时溢出。 */
            okay = 0; /* 拒绝非法完美数量。 */
        if (item.stats.great < 0 || item.stats.great > MAX_NOTES) /* 限制优秀数量。 */
            okay = 0; /* 拒绝非法优秀数量。 */
        if (item.stats.good < 0 || item.stats.good > MAX_NOTES) /* 限制良好数量。 */
            okay = 0; /* 拒绝非法良好数量。 */
        if (okay) /* 只有整数范围有效时才计算权重。 */
            item.stats.units = item.stats.perfect * 1000 + item.stats.great * 700 + item.stats.good * 400; /* 重算权重。 */
        if (okay && !record_valid(&item)) /* 进一步核对成绩的数量关系。 */
            okay = 0; /* 拒绝无法由真实判定生成的记录。 */
        for (i = 0; i < count; i++) /* 文件内不允许同一账号同一榜单重复。 */
        { /* 查重循环开始。 */
            if (records[i].song == item.song && records[i].difficulty == item.difficulty && strcmp(records[i].name, item.name) == 0) /* 对比最佳成绩键。 */
                okay = 0; /* 重复数据视为格式错误。 */
        } /* 查重循环结束。 */
        if (okay) /* 当前记录完整有效。 */
            records[count++] = item; /* 写入输出数组。 */
    } /* 成绩读取循环结束。 */
    if (ferror(file)) /* 检查读取中断。 */
        okay = 0; /* 文件没有完整读出。 */
    fclose(file); /* 关闭文件。 */
    if (!okay) /* 任意一行出错时保留整份原文件。 */
    { /* 错误分支开始。 */
        fail(error, "成绩文件格式错误，原文件已保留，禁止覆盖。"); /* 提示具体处理原则。 */
        return -1; /* 不返回部分可用记录。 */
    } /* 错误分支结束。 */
    error[0] = 0; /* 清除旧错误。 */
    return count; /* 返回有效成绩数。 */
} /* 函数结束。 */

int record_save(const Record *record, char *error) /* 每个账号每首歌每个难度只保留最佳成绩。 */
{ /* 函数开始。 */
    Record records[MAX_RECORDS]; /* 保存全部最佳成绩，固定容量。 */
    int count; /* 当前记录数。 */
    int index = -1; /* 是否已经有当前账号的对应记录。 */
    int i; /* 遍历所有记录。 */
    FILE *file; /* 本次写入的临时文件。 */
    if (!record_valid(record)) /* 未完成或非法统计不进入排行榜。 */
        return fail(error, "本局成绩不完整或不合法，不能保存。"); /* 输出保存条件。 */
    count = records_load(records, error); /* 先读取并验证旧文件。 */
    if (count < 0) /* 旧文件损坏时禁止覆盖。 */
        return 0; /* 返回失败。 */
    for (i = 0; i < count; i++) /* 查找相同账号、歌曲和难度。 */
    { /* 查找循环开始。 */
        if (records[i].song == record->song && records[i].difficulty == record->difficulty && strcmp(records[i].name, record->name) == 0) /* 当前榜单的同一账号。 */
            index = i; /* 保存已有记录下标。 */
    } /* 查找循环结束。 */
    if (index >= 0) /* 已有该账号的历史最佳成绩。 */
    { /* 旧记录分支开始。 */
        Result oldResult = stats_result(&records[index].stats, records[index].noteCount); /* 计算旧成绩。 */
        Result newResult = stats_result(&record->stats, record->noteCount); /* 计算新成绩。 */
        if (newResult.score <= oldResult.score) /* 较低或相同分数不覆盖旧最佳。 */
            return 1; /* 保存规则已满足，不重复改写文件。 */
    } /* 旧记录分支结束。 */
    else /* 此账号在该榜单上尚无成绩。 */
    { /* 新记录分支开始。 */
        if (count >= MAX_RECORDS) /* 防止固定数组写越界。 */
            return fail(error, "最佳成绩数量达到二百条上限。"); /* 提示容量限制。 */
        index = count++; /* 为新最佳成绩分配数组位置。 */
    } /* 新记录分支结束。 */
    records[index] = *record; /* 保存待提交的新最佳成绩。 */
    file = open_write("scores.tmp", error); /* 打开临时文件。 */
    if (file == NULL) /* 文件不可写时保留旧成绩。 */
        return 0; /* 返回保存失败。 */
    fprintf(file, "NOTE_COLLISION_V2_SCORES 1\n"); /* 写入格式头和版本。 */
    for (i = 0; i < count; i++) /* 重新写出全部最佳成绩。 */
    { /* 保存循环开始。 */
        const Record *item = &records[i]; /* 取得当前成绩条目。 */
        fprintf(file, "%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n", item->name, item->song, item->difficulty, item->noteCount, item->stats.perfect, item->stats.great, item->stats.good, item->stats.miss, item->stats.maxCombo, item->date); /* 保存原始统计而非可信外来分数。 */
    } /* 保存循环结束。 */
    return finish_write(file, "scores.tmp", "scores.txt", error); /* 成功后替换正式成绩文件。 */
} /* 函数结束。 */

int ranking_make(const Record *records, int count, int song, int difficulty, int order[MAX_RECORDS]) /* 生成排序下标。 */
{ /* 函数开始。 */
    int used = 0; /* 符合当前榜单条件的记录数。 */
    int i; /* 遍历全部成绩。 */
    int j; /* 使用基础插入排序排列下标。 */
    for (i = 0; i < count; i++) /* 筛选歌曲和难度。 */
    { /* 筛选循环开始。 */
        if (records[i].song == song && records[i].difficulty == difficulty) /* 当前记录属于目标榜单。 */
            order[used++] = i; /* 只保存下标，不复制大结构。 */
    } /* 筛选循环结束。 */
    for (i = 1; i < used; i++) /* 从第二条开始插入到已有有序区。 */
    { /* 排序循环开始。 */
        int key = order[i]; /* 保存当前需要插入的下标。 */
        int score = stats_result(&records[key].stats, records[key].noteCount).score; /* 当前分数。 */
        j = i - 1; /* 从此前最后一条开始比较。 */
        while (j >= 0) /* 逐个将较低分数向后移动。 */
        { /* 插入循环开始。 */
            int other = stats_result(&records[order[j]].stats, records[order[j]].noteCount).score; /* 此前分数。 */
            if (other >= score) /* 同分保留原有顺序，便于稳定显示。 */
                break; /* 已找到合适的插入位置。 */
            order[j + 1] = order[j]; /* 将较低分记录向后移动。 */
            j--; /* 继续向前比较。 */
        } /* 插入循环结束。 */
        order[j + 1] = key; /* 插入当前记录下标。 */
    } /* 排序循环结束。 */
    return used; /* 返回已排序记录数。 */
} /* 函数结束。 */

int settings_load(Settings *settings, char *error) /* 读取简单设置文件。 */
{ /* 函数开始。 */
    char path[256]; /* 设置文件路径。 */
    FILE *file; /* 设置文件指针。 */
    char line[100]; /* 读取完整设置行。 */
    int used = 0; /* 检查解析尾部。 */
    int okay = 0; /* 记录文件是否合法。 */
    Settings candidate = {70, 0}; /* 默认音量七十，校准零毫秒。 */
    *settings = candidate; /* 出错时也提供可用默认设置。 */
    file_path(path, "settings.txt"); /* 得到设置文件路径。 */
    file = fopen(path, "rb"); /* 只读打开文件。 */
    if (file == NULL) /* 不存在时使用默认值。 */
    { /* 打开失败分支开始。 */
        if (errno == ENOENT) /* 首次运行没有设置属于正常情况。 */
            return 1; /* 默认值有效。 */
        return fail(error, "无法读取设置，暂时使用默认值。"); /* 不能隐藏文件访问错误。 */
    } /* 打开失败分支结束。 */
    if (fgets(line, sizeof(line), file) != NULL) /* 设置只占一行。 */
    { /* 行解析分支开始。 */
        trim_newline(line); /* 去除行末换行。 */
        okay = sscanf(line, "NOTE_COLLISION_V2_SETTINGS 1 %d %d%n", &candidate.volume, &candidate.offset, &used) == 2; /* 读取音量和偏移。 */
        if (line[used] != 0 || fgetc(file) != EOF || ferror(file)) /* 检查多余内容和读取错误。 */
            okay = 0; /* 拒绝损坏文件。 */
    } /* 行解析分支结束。 */
    fclose(file); /* 关闭文件。 */
    if (candidate.volume < 0 || candidate.volume > 100 || candidate.offset < -200 || candidate.offset > 200) /* 限制设置范围。 */
        okay = 0; /* 越界设置不生效。 */
    if (!okay) /* 不自动覆盖损坏文件。 */
        return fail(error, "设置文件损坏，原文件已保留，暂时使用默认值。"); /* 提示用户手工检查。 */
    *settings = candidate; /* 接受合法设置。 */
    return 1; /* 设置读取成功。 */
} /* 函数结束。 */

int settings_save(const Settings *settings, char *error) /* 校验范围后保存设置。 */
{ /* 函数开始。 */
    FILE *file; /* 临时设置文件指针。 */
    if (settings->volume < 0 || settings->volume > 100 || settings->offset < -200 || settings->offset > 200) /* 检查范围。 */
        return fail(error, "音量应为 0~100，输入偏移应为 -200~200 毫秒。"); /* 拒绝非法值。 */
    file = open_write("settings.tmp", error); /* 打开同目录临时文件。 */
    if (file == NULL) /* 文件无法创建时保留旧值。 */
        return 0; /* 返回失败。 */
    fprintf(file, "NOTE_COLLISION_V2_SETTINGS 1 %d %d\n", settings->volume, settings->offset); /* 保存两项设置。 */
    return finish_write(file, "settings.tmp", "settings.txt", error); /* 提交新设置。 */
} /* 函数结束。 */
