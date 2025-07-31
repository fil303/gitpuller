#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_BRANCHES 512
#define MAX_NAME 256
#define MAX_ROWS 50

typedef struct {
    int checkout_idx;
    int pull_idx;
} RowSelection;

char branches[MAX_BRANCHES][MAX_NAME];
int branch_count = 0;

// dynamic rows
RowSelection rows[MAX_ROWS];
int row_count = 1; // start with 1 rows

// utility: run a shell command and capture output
void run_cmd(const char *cmd) {
    system(cmd);
}

int branch_exists_local(const char *name) {
    for (int i = 0; i < branch_count; i++) {
        if (strcmp(branches[i], name) == 0) return 1;
    }
    return 0;
}

void fetch_and_track_all() {
    run_cmd("git fetch --all --quiet");
    FILE *fp = popen("git branch -r", "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        line[strcspn(p, "\n")] = 0;
        if (strstr(p, "HEAD") == p) continue; // skip HEAD
        // remote/branch format
        char local[MAX_NAME];
        const char *slash = strchr(p, '/');
        if (!slash) continue;
        strcpy(local, slash+1);
        // create local tracking if not exists
        char cmd[600];
        snprintf(cmd, sizeof(cmd), "git show-ref --verify --quiet refs/heads/%s || git branch --track %s %s >/dev/null 2>&1", local, local, p);
        run_cmd(cmd);
    }
    pclose(fp);
}

void load_local_branches() {
    branch_count = 0;
    FILE *fp = popen("git branch -r", "r");
    if (!fp) {
        perror("Failed to run git branch");
        exit(1);
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[strlen(line) - 1] == '\n')
            line[strlen(line) - 1] = '\0';

        // Remove prefix like "origin/"
        char *branch = strstr(line, "origin/");
        if (branch) branch += 7; else branch = line;

        // Skip HEAD or duplicates
        if (strstr(branch, "->") || strcmp(branch, "HEAD") == 0)
            continue;

        // Remove spaces or asterisk at beginning
        while (*branch == ' ' || *branch == '*') branch++;

        // branches[branch_count++] = strdup(branch);
        strcpy(branches[branch_count++], strdup(branch));
            if (branch_count >= MAX_BRANCHES) break;
    }
    pclose(fp);

    if (branch_count == 0) {
        strcpy(branches[branch_count++], "(no branches)");
    }
}

int longest_branch_width() {
    int maxw = 0;
    for (int i = 0; i < branch_count; i++) {
        int len = strlen(branches[i]);
        if (len > maxw) maxw = len;
    }
    int term_w, term_h;
    getmaxyx(stdscr, term_h, term_w);
    int target = maxw + 6;
    if (target > term_w - 4) target = term_w - 4; // leave padding
    if (target < 30) target = 30;
    return target;
}

void draw_ui(int highlight_row, int highlight_col, int button_highlight, int add_highlight) {
    clear();
    int term_w, term_h;
    getmaxyx(stdscr, term_h, term_w);
    int col_w = (term_w - 10) / 2;

    mvprintw(0, 0, "Arrows/TAB to move, Enter to select, q to quit.");
    mvprintw(1, 2, "%-*s    %-*s", col_w, "[ Checkout ]", col_w, "[ Pull From ]");
    for (int r = 0; r < row_count; r++) {
        int y = 3 + r;
        int x0 = 2;
        int x1 = x0 + col_w + 6;

        if (!button_highlight && !add_highlight && highlight_row == r && highlight_col == 0) attron(A_REVERSE);
        mvprintw(y, x0, "%-*s", col_w + 2, branches[rows[r].checkout_idx]);
        if (!button_highlight && !add_highlight && highlight_row == r && highlight_col == 0) attroff(A_REVERSE);

        if (!button_highlight && !add_highlight && highlight_row == r && highlight_col == 1) attron(A_REVERSE);
        mvprintw(y, x1, "%-*s", col_w + 2, branches[rows[r].pull_idx]);
        if (!button_highlight && !add_highlight && highlight_row == r && highlight_col == 1) attroff(A_REVERSE);
    }

    int add_y = 3 + row_count + 1;
    if (add_highlight) attron(A_REVERSE);
    mvprintw(add_y, 0, "[ + Add Row ]");
    if (add_highlight) attroff(A_REVERSE);

    int start_y = add_y + 2;
    if (button_highlight) attron(A_REVERSE);
    mvprintw(start_y, 0, "[   START   ]");
    if (button_highlight) attroff(A_REVERSE);

    refresh();
}

int dropdown_select(int starty, int startx, int current_index) {
    int win_w = longest_branch_width();
    int win_h = 20;
    WINDOW *win = newwin(win_h, win_w, starty, startx);
    keypad(win, TRUE);
    box(win, 0, 0);

    char filter[128] = "";
    int filtered[MAX_BRANCHES];
    int filtered_count;
    int highlight = current_index;
    int offset = 0;

    void update_filter() {
        filtered_count = 0;
        for (int i = 0; i < branch_count; i++) {
            if (strlen(filter) == 0 || strcasestr(branches[i], filter)) {
                filtered[filtered_count++] = i;
            }
        }
        if (filtered_count == 0) {
            filtered[filtered_count++] = -1;
        }
        // reset highlight if current not in filter
        int found = 0;
        for (int k = 0; k < filtered_count; k++) {
            if (filtered[k] == highlight) { found = 1; break; }
        }
        if (!found && filtered_count > 0 && filtered[0] != -1) {
            highlight = filtered[0];
        }
    }

    update_filter();

    int ch;
    while (1) {
        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, "Filter: %s", filter);

        int hi_pos = 0;
        for (int k = 0; k < filtered_count; k++) if (filtered[k] == highlight) { hi_pos = k; break; }
        if (hi_pos < offset) offset = hi_pos;
        if (hi_pos >= offset + win_h - 2) offset = hi_pos - (win_h - 3);

        for (int i = 0; i < win_h-2; i++) {
            int idxpos = offset + i;
            if (idxpos >= filtered_count) {
                mvwprintw(win, i+1, 1, "%-*s", win_w-2, "");
                continue;
            }
            int bidx = filtered[idxpos];
            const char *text = (bidx >= 0) ? branches[bidx] : "(no match)";
            if (bidx == highlight) wattron(win, A_REVERSE);
            mvwprintw(win, i+1, 1, "%-*s", win_w-2, text);
            if (bidx == highlight) wattroff(win, A_REVERSE);
        }
        wrefresh(win);

        ch = wgetch(win);
        if (ch == KEY_UP) {
            int hi = 0;
            for (int k = 0; k < filtered_count; k++) if (filtered[k] == highlight) { hi = k; break; }
            if (hi > 0) highlight = filtered[hi-1];
        } else if (ch == KEY_DOWN) {
            int hi = 0;
            for (int k = 0; k < filtered_count; k++) if (filtered[k] == highlight) { hi = k; break; }
            if (hi < filtered_count-1 && filtered[hi+1]>=0) highlight = filtered[hi+1];
        } else if (ch == '\n') {
            delwin(win);
            return highlight;
        } else if (ch == 27) { // ESC
            delwin(win);
            return current_index;
        } else if (ch == KEY_BACKSPACE || ch == 127) {
            int len = strlen(filter);
            if (len > 0) { filter[len-1] = '\0'; update_filter(); }
        } else if (isprint(ch)) {
            int len = strlen(filter);
            if (len < (int)sizeof(filter)-1) {
                filter[len] = (char)ch;
                filter[len+1] = '\0';
                update_filter();
            }
        }
    }
}

int main() {
    fetch_and_track_all();
    load_local_branches();

    for (int i = 0; i < row_count; i++) {
        rows[i].checkout_idx = 0;
        rows[i].pull_idx = 0;
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    int highlight_row = 0;
    int highlight_col = 0;
    int add_highlight = 0;
    int button_highlight = 0;

    draw_ui(highlight_row, highlight_col, button_highlight, add_highlight);

    int ch;
    while ((ch = getch())) {
        if (ch == 'q') break;

        if (button_highlight) {
            if (ch == KEY_UP) { button_highlight = 0; add_highlight = 1; }
            else if (ch == '\n') {
                char cmd[600];
                for (int i = 0; i < row_count; i++) {
                    const char *checkout_branch = branches[rows[i].checkout_idx];
                    const char *pull_branch = branches[rows[i].pull_idx];

                    // Checkout the branch
                    snprintf(cmd, sizeof(cmd), "git checkout %s >/dev/null 2>&1", checkout_branch);
                    int ret = system(cmd);
                    if (ret != 0) {
                        move(row_count + 10, 0); clrtoeol();
                        printw( "Error: Failed to checkout '%s'", checkout_branch);
                        refresh(); getch(); break;
                    }

                    // Pull from the same branch (safe refresh)
                    snprintf(cmd, sizeof(cmd), "git pull origin %s >/dev/null 2>&1", checkout_branch);
                    ret = system(cmd);
                    if (ret != 0) {
                        move(row_count + 11, 0); clrtoeol();
                        printw("Error: Pull failed from origin/%s", checkout_branch);
                        refresh(); getch(); break;
                    }

                    // Pull from the selected "pull from" branch
                    snprintf(cmd, sizeof(cmd), "git pull origin %s >/dev/null 2>&1", pull_branch);
                    FILE *merge_fp = popen(cmd, "r");
                    if (!merge_fp) {
                        move(row_count + 12, 0); clrtoeol();
                        printw("Error: Failed to pull from %s", pull_branch);
                        refresh(); getch(); break;
                    }

                    char line[512];
                    int conflict = 0;
                    while (fgets(line, sizeof(line), merge_fp)) {
                        if (strstr(line, "CONFLICT")) {
                            conflict = 1;
                            break;
                        }
                    }
                    pclose(merge_fp);

                    if (conflict) {
                        move(row_count + 13, 0); clrtoeol();
                        printw("Conflict detected while pulling from %s into %s", pull_branch, checkout_branch);
                        refresh(); getch(); break;
                    }

                    // No conflict: push
                    snprintf(cmd, sizeof(cmd), "git push origin %s > /dev/null 2>&1", checkout_branch);
                    ret = system(cmd);
                    if (ret != 0) {
                        move(row_count + 13, 0); clrtoeol();
                        printw("X: Failed to push '%s' to origin", checkout_branch);
                        refresh(); getch(); break;
                    }
                }
                move(row_count + 15, 0); clrtoeol();
                printw("Merge complete.");
                refresh(); getch(); break;
            }
        }
        else if (add_highlight) {
            if (ch == KEY_UP) {
                add_highlight = 0;
                highlight_row = row_count-1;
                highlight_col = 0;
            } else if (ch == KEY_DOWN) {
                add_highlight = 0;
                button_highlight = 1;
            } else if (ch == '\n') {
                if (row_count < MAX_ROWS) {
                    rows[row_count].checkout_idx = 0;
                    rows[row_count].pull_idx = 0;
                    row_count++;
                }
            }
        }
        else {
            if (ch == KEY_UP) {
                if (highlight_row > 0) highlight_row--;
            } else if (ch == KEY_DOWN) {
                if (highlight_row < row_count-1) highlight_row++;
                else { add_highlight = 1; }
            } else if (ch == '\t') {
                highlight_col = (highlight_col == 0) ? 1 : 0;
            } else if (ch == '\n') {
                int idx = (highlight_col == 0)
                    ? rows[highlight_row].checkout_idx
                    : rows[highlight_row].pull_idx;
                int chosen = dropdown_select(3+highlight_row, (highlight_col==0)?0:40, idx);
                if (highlight_col == 0)
                    rows[highlight_row].checkout_idx = chosen;
                else
                    rows[highlight_row].pull_idx = chosen;
            }
        }

        draw_ui(highlight_row, highlight_col, button_highlight, add_highlight);
    }

    endwin();
    return 0;
}
