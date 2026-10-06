/*
 * lang/ui_zh.h -- Chinese interface strings (default build).
 *
 * Every piece of visible text lives in a lang/ui_XX.h header so the same code produces a
 * Chinese and an English build (fm.elf / fmen.elf).  Strings must stay inside 320 px:
 * ASCII 8 px per cell, CJK 16 px per cell, so a Chinese line may be up to ~19 characters
 * and an English line up to ~39.
 */
#ifndef UI_LANG_H
#define UI_LANG_H

/* list view */
#define UI_ITEMS_SUFFIX " 项"
#define UI_EMPTY_DIR "（空目录）"
#define UI_COPY_BANNER_PREFIX "把"
#define UI_COPY_BANNER_MID "复制到"
#define UI_COPY_MODE_HINT "复制模式：回车=粘贴到此处  esc=取消"

/* help page */
#define UI_HELP_TITLE "帮助"
static const char *const UI_HELP_LINES[] = {
    " esc          退出程序",
    " 上/下        移动选择",
    " 回车 / 右    打开文件或进入目录",
    " 左           返回上一级",
    " help / h     显示或隐藏本页",
    " v 查看   i 信息   r 刷新",
    " x 删除   c 复制   y 重命名   m 新建",
    " 查看器：上/下 翻行  左/右 翻块",
    "         esc 返回",
    " 对话框：回车=是  esc=否",
};

/* info page */
#define UI_INFO_TITLE "文件信息"
#define UI_INFO_NONE "（无）"
#define UI_INFO_NAME "名称："
#define UI_INFO_PATH "路径："
#define UI_INFO_SIZE "大小："
#define UI_INFO_TIME "时间："
#define UI_INFO_ATTR "属性："
#define UI_INFO_TYPE "类型："
#define UI_INFO_DIR "目录"
#define UI_INFO_FILE "文件"

/* viewer / dialogs */
#define UI_VIEW_EMPTY "(空文件或读取失败)"
#define UI_VIEW_FAIL "(读取失败)"
#define UI_CONFIRM_HINT "回车/y = 是    esc/n = 否"
#define UI_CANNOT_ENTER "无法进入"
#define UI_DELETE_Q_PRE "删除 '"
#define UI_DELETE_Q_POST "'？"
#define UI_DELETING "正在删除，请稍候"
#define UI_DELETE_FAIL "删除失败"
#define UI_MKDIR_PROMPT "新建目录名"
#define UI_MKDIR_FAIL "创建失败"
#define UI_RENAME_PROMPT "重命名为"
#define UI_RENAME_FAIL "重命名失败"
#define UI_COPY_SAME "目标与源相同"
#define UI_COPYING "正在复制，请稍候"
#define UI_COPIED "已复制："
#define UI_COPY_FAIL "复制失败"
#define UI_QUIT_Q "退出文件管理器？"

#endif
