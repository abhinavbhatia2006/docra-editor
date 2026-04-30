#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <pthread.h>
#include "../include/client.h"

int my_cursor_row = 0;
int my_cursor_col = 0;

void tui_init(void) {
    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    timeout(30);

    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_WHITE, COLOR_BLUE);
        init_pair(2, COLOR_WHITE, COLOR_GREEN);
        init_pair(3, COLOR_WHITE, COLOR_RED);
        init_pair(4, COLOR_WHITE, COLOR_MAGENTA);
        init_pair(5, COLOR_WHITE, COLOR_YELLOW);
        init_pair(6, COLOR_WHITE, COLOR_CYAN);
    }
}

void tui_cleanup(void) {
    endwin();
}

void tui_render(void) {
    pthread_mutex_lock(&client_mutex);
    
    erase();
    
    int current_row = 0;
    int current_col = 0;
    
    CharNode* current = local_document_state.document_head->next;
    
    while (current != local_document_state.document_tail) {
        if (!current->is_deleted) {
            if (current->value == '\n') {
                current_row++;
                current_col = 0;
            } else {
                mvaddch(current_row, current_col, current->value);
                current_col++;
            }
        }
        current = current->next;
    }

    for (int i = 0; i < local_document_state.client_count; i++) {
        ClientInfo* remote_client = local_document_state.clients[i];
        if (remote_client->site_id != my_site_id) {
            int color_idx = (remote_client->site_id % 6) + 1;
            attron(COLOR_PAIR(color_idx));
            
            char char_under_cursor = mvinch(remote_client->cursor_row, remote_client->cursor_col) & A_CHARTEXT;
            if (char_under_cursor == ' ' || char_under_cursor == '\0') {
                char_under_cursor = ' ';
            }
            
            mvaddch(remote_client->cursor_row, remote_client->cursor_col, char_under_cursor);
            attroff(COLOR_PAIR(color_idx));
        }
    }

    attron(A_REVERSE);
    mvprintw(LINES - 1, 0, " DOCRA | Room: %s | Role: %s | Site ID: %d | ESC to quit ", 
             local_document_state.room_name, 
             (my_role == ROLE_ADMIN) ? "ADMIN" : (my_role == ROLE_EDITOR) ? "EDITOR" : "GUEST",
             my_site_id);
    attroff(A_REVERSE);

    move(my_cursor_row, my_cursor_col);
    
    refresh();
    pthread_mutex_unlock(&client_mutex);
}

// --- THE NEW SNAPPING ENGINE ---
static void snap_cursor_to_document(void) {
    pthread_mutex_lock(&client_mutex);

    int max_row = 0;
    int target_row_length = 0;
    int current_col_count = 0;

    // Scan the true mathematical size of the document
    CharNode* current = local_document_state.document_head->next;
    while (current != local_document_state.document_tail) {
        if (!current->is_deleted) {
            if (current->value == '\n') {
                if (max_row == my_cursor_row) {
                    target_row_length = current_col_count;
                }
                max_row++;
                current_col_count = 0;
            } else {
                current_col_count++;
            }
        }
        current = current->next;
    }
    // Catch the length of the final line
    if (max_row == my_cursor_row) {
        target_row_length = current_col_count;
    }

    // 1. Constrain Row Bounds
    if (my_cursor_row > max_row) {
        my_cursor_row = max_row;
        target_row_length = current_col_count; // Grab length of the true last row
    }
    if (my_cursor_row < 0) {
        my_cursor_row = 0;
    }

    // 2. Constrain Col Bounds (Prevents wandering into the void)
    if (my_cursor_col > target_row_length) {
        my_cursor_col = target_row_length;
    }
    if (my_cursor_col < 0) {
        my_cursor_col = 0;
    }

    pthread_mutex_unlock(&client_mutex);
}

void tui_input_loop(void) {
    int ch;
    while (1) {
        tui_render();
        
        ch = getch();
        
        if (ch == ERR) continue;
        if (ch == 27) break; // ESCAPE

        switch (ch) {
            case KEY_UP:
                my_cursor_row--;
                break;
            case KEY_DOWN:
                my_cursor_row++;
                break;
            case KEY_LEFT:
                my_cursor_col--;
                // If moving left past 0, jump up to the previous line
                if (my_cursor_col < 0 && my_cursor_row > 0) {
                    my_cursor_row--;
                    my_cursor_col = 99999; // The snapper will pull this back to the exact end of that line
                }
                break;
            case KEY_RIGHT:
                my_cursor_col++;
                break;
            case KEY_BACKSPACE:
            case 127:
            case '\b':
                network_send_delete();
                my_cursor_col--;
                if (my_cursor_col < 0 && my_cursor_row > 0) {
                    my_cursor_row--;
                    my_cursor_col = 99999; 
                }
                break;
            case KEY_ENTER:
            case '\n':
            case '\r':
                network_send_insert('\n');
                my_cursor_row++;
                my_cursor_col = 0;
                break;
            default:
                if (ch >= 32 && ch <= 126) {
                    network_send_insert((char)ch);
                    my_cursor_col++;
                }
                break;
        }

        // Apply physical constraints BEFORE sending the cursor update to the server
        snap_cursor_to_document();
        network_send_cursor(my_cursor_row, my_cursor_col);
    }
}