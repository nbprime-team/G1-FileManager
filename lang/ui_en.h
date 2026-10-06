/*
 * lang/ui_en.h -- English interface strings (build with: make en).
 * Kept line for line identical to ui_zh.h so both builds behave the same.
 */
#ifndef UI_LANG_H
#define UI_LANG_H

/* list view */
#define UI_ITEMS_SUFFIX " items"
#define UI_EMPTY_DIR "(empty directory)"
#define UI_COPY_BANNER_PREFIX "copy "
#define UI_COPY_BANNER_MID " to "
#define UI_COPY_MODE_HINT "copy mode: enter = paste, esc = cancel"

/* help page */
#define UI_HELP_TITLE "help"
static const char *const UI_HELP_LINES[] = {
    " esc          quit",
    " up / down    move selection",
    " enter/right  open file or enter dir",
    " left         go up one directory",
    " help / h     show or hide this page",
    " v view  i info  r refresh",
    " x del   c copy  y rename  m mkdir",
    " viewer: up/down line  left/right block",
    "         esc back",
    " dialog: enter = yes   esc = no",
};

/* info page */
#define UI_INFO_TITLE "file info"
#define UI_INFO_NONE "(none)"
#define UI_INFO_NAME "name: "
#define UI_INFO_PATH "path: "
#define UI_INFO_SIZE "size: "
#define UI_INFO_TIME "time: "
#define UI_INFO_ATTR "attr: "
#define UI_INFO_TYPE "type: "
#define UI_INFO_DIR "directory"
#define UI_INFO_FILE "file"

/* viewer / dialogs */
#define UI_VIEW_EMPTY "(empty file or read failed)"
#define UI_VIEW_FAIL "(read failed)"
#define UI_CONFIRM_HINT "enter/y = yes    esc/n = no"
#define UI_CANNOT_ENTER "cannot enter"
#define UI_DELETE_Q_PRE "delete '"
#define UI_DELETE_Q_POST "'?"
#define UI_DELETING "deleting, please wait"
#define UI_DELETE_FAIL "delete failed"
#define UI_MKDIR_PROMPT "new directory name"
#define UI_MKDIR_FAIL "mkdir failed"
#define UI_RENAME_PROMPT "rename to"
#define UI_RENAME_FAIL "rename failed"
#define UI_COPY_SAME "target equals source"
#define UI_COPYING "copying, please wait"
#define UI_COPIED "copied: "
#define UI_COPY_FAIL "copy failed"
#define UI_QUIT_Q "quit the file manager?"

#endif
