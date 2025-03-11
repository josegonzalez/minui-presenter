#include <fcntl.h>
#include <getopt.h>
#include <msettings.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef USE_SDL2
#include <SDL2/SDL_ttf.h>
#else
#include <SDL/SDL_ttf.h>
#endif

#include "defines.h"
#include "api.h"
#include "utils.h"

SDL_Surface *screen = NULL;

#ifdef USE_SDL2
bool use_sdl2 = true;
#else
bool use_sdl2 = false;
#endif

enum list_result_t
{
    ExitCodeSuccess = 0,
    ExitCodeError = 1,
    ExitCodeCancelButton = 2,
    ExitCodeMenuButton = 3,
    ExitCodeActionButton = 4,
    ExitCodeStartButton = 5,
    ExitCodeTimeout = 124,
    ExitCodeKeyboardInterrupt = 130,
};
typedef int ExitCode;

// log_error logs a message to stderr for debugging purposes
void log_error(const char *msg)
{
    // Set stderr to unbuffered mode
    setvbuf(stderr, NULL, _IONBF, 0);
    fprintf(stderr, "%s\n", msg);
}

// log_info logs a message to stdout for debugging purposes
void log_info(const char *msg)
{
    // Set stdout to unbuffered mode
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("%s\n", msg);
}

// Fonts holds the fonts for the list
struct Fonts
{
    // the size of the font to use for the list
    int size;
    // the large font to use for the list
    TTF_Font *large;
    // the medium font to use for the list
    TTF_Font *medium;
    // the small font to use for the list
    TTF_Font *small;
};

enum MessageAlignment
{
    MessageAlignmentTop,
    MessageAlignmentMiddle,
    MessageAlignmentBottom,
};

struct DisplayState
{
    // the background color to use for the list
    int background_color;
    // path to the background image to use for the list
    char background_image[1024];
    // the message to display
    char message[1024];
    // the alignment of the message
    enum MessageAlignment message_alignment;
};

// AppState holds the current state of the application
struct AppState
{
    // whether the screen needs to be redrawn
    int redraw;
    // whether the app should exit
    int quitting;
    // the exit code to return
    int exit_code;
    // whether to show the hardware group
    int show_hardware_group;
    // the button to display on the Action button
    char action_button[1024];
    // whether to show the Action button
    bool action_show;
    // the text to display on the Action button
    char action_text[1024];
    // the button to display on the Confirm button
    char confirm_button[1024];
    // whether to show the Confirm button
    bool confirm_show;
    // the text to display on the Confirm button
    char confirm_text[1024];
    // the button to display on the Cancel button
    char cancel_button[1024];
    // whether to show the Cancel button
    bool cancel_show;
    // the text to display on the Cancel button
    char cancel_text[1024];
    // the seconds to display the message for before timing out
    int timeout_seconds;
    // whether to show the time left
    bool show_time_left;
    // current display state index
    int current_display_state_index;
    // the start time of the presentation
    struct timeval start_time;
    // the fonts to use for the list
    struct Fonts fonts;
    // the display states
    struct DisplayState display_states[1024];
};

// handle_input interprets input events and mutates app state
void handle_input(struct AppState *state)
{
    PAD_poll();

    bool is_action_button_pressed = false;
    bool is_confirm_button_pressed = false;
    bool is_cancel_button_pressed = false;
    if (PAD_justReleased(BTN_A))
    {
        if (strcmp(state->action_button, "A") == 0)
        {
            is_action_button_pressed = true;
        }
        else if (strcmp(state->confirm_button, "A") == 0)
        {
            is_confirm_button_pressed = true;
        }
        else if (strcmp(state->cancel_button, "A") == 0)
        {
            is_cancel_button_pressed = true;
        }
    }
    else if (PAD_justReleased(BTN_B))
    {
        if (strcmp(state->action_button, "B") == 0)
        {
            is_action_button_pressed = true;
        }
        else if (strcmp(state->confirm_button, "B") == 0)
        {
            is_confirm_button_pressed = true;
        }
        else if (strcmp(state->cancel_button, "B") == 0)
        {
            is_cancel_button_pressed = true;
        }
    }
    else if (PAD_justReleased(BTN_X))
    {
        if (strcmp(state->action_button, "X") == 0)
        {
            is_action_button_pressed = true;
        }
        else if (strcmp(state->confirm_button, "X") == 0)
        {
            is_confirm_button_pressed = true;
        }
        else if (strcmp(state->cancel_button, "X") == 0)
        {
            is_cancel_button_pressed = true;
        }
    }
    else if (PAD_justReleased(BTN_Y))
    {
        if (strcmp(state->action_button, "Y") == 0)
        {
            is_action_button_pressed = true;
        }
        else if (strcmp(state->confirm_button, "Y") == 0)
        {
            is_confirm_button_pressed = true;
        }
        else if (strcmp(state->cancel_button, "Y") == 0)
        {
            is_cancel_button_pressed = true;
        }
    }

    if (is_action_button_pressed)
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeActionButton;
        return;
    }

    if (is_confirm_button_pressed)
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeSuccess;
        return;
    }

    if (is_cancel_button_pressed)
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeCancelButton;
        return;
    }

    if (PAD_justReleased(BTN_START))
    {
        state->redraw = 0;
        state->quitting = 1;
        state->exit_code = ExitCodeStartButton;
        return;
    }
}

struct Message
{
    char message[1024];
    int width;
};

void strtrim(char *s)
{
    char *p = s;
    int l = strlen(p);

    if (l == 0)
    {
        return;
    }

    while (isspace(p[l - 1]))
    {
        p[--l] = 0;
    }

    while (*p && isspace(*p))
    {
        ++p;
        --l;
    }

    memmove(s, p, l + 1);
}

// draw_screen interprets the app state and draws it to the screen
void draw_screen(SDL_Surface *screen, struct AppState *state)
{
    // draw the button group on the button-right
    // only two buttons can be displayed at a time
    if (state->confirm_show)
    {
        if (state->cancel_show)
        {
            GFX_blitButtonGroup((char *[]){state->cancel_button, state->cancel_text, state->confirm_button, state->confirm_text, NULL}, 1, screen, 1);
        }
        else
        {
            GFX_blitButtonGroup((char *[]){state->confirm_button, state->confirm_text, NULL}, 1, screen, 1);
        }
    }
    else if (state->cancel_show)
    {
        GFX_blitButtonGroup((char *[]){state->cancel_button, state->cancel_text, NULL}, 1, screen, 1);
    }

    if (state->show_time_left && state->timeout_seconds > 0)
    {
        struct timeval current_time;
        gettimeofday(&current_time, NULL);

        int time_left = state->timeout_seconds - (current_time.tv_sec - state->start_time.tv_sec);
        if (time_left <= 0)
        {
            time_left = 0;
        }

        char time_left_str[1024];
        if (time_left == 1)
        {
            snprintf(time_left_str, sizeof(time_left_str), "time left: %d second", time_left);
        }
        else
        {
            snprintf(time_left_str, sizeof(time_left_str), "time left: %d seconds", time_left);
        }

        SDL_Surface *text = TTF_RenderUTF8_Blended(state->fonts.small, time_left_str, COLOR_WHITE);
        SDL_Rect pos = {
            SCALE1(PADDING),
            SCALE1(PADDING),
            text->w,
            text->h};
        SDL_BlitSurface(text, NULL, screen, &pos);
    }

    int message_padding = SCALE1(PADDING + BUTTON_PADDING);

    struct DisplayState *display_state = &state->display_states[state->current_display_state_index];

    // get the width and height of every word in the message
    struct Message words[1024];
    int word_count = 0;
    char original_message[1024];
    strncpy(original_message, display_state->message, sizeof(original_message) - 1);
    char *word = strtok(original_message, " ");
    int word_height = 0;
    while (word != NULL)
    {
        int word_width;
        strtrim(word);
        if (strcmp(word, "") == 0)
        {
            continue;
        }

        TTF_SizeUTF8(state->fonts.large, word, &word_width, &word_height);
        strncpy(words[word_count].message, word, sizeof(word) - 1);
        words[word_count].width = word_width;
        word_count++;
        word = strtok(NULL, " ");
    }

    int letter_width = 0;
    TTF_SizeUTF8(state->fonts.large, "A", &letter_width, NULL);

    // construct a list of messages that can be displayed on a single line
    // if the message is too long to be displayed on a single line,
    // the message will be wrapped onto multiple lines
    struct Message messages[MAIN_ROW_COUNT];
    for (int i = 0; i < MAIN_ROW_COUNT; i++)
    {
        strncpy(messages[i].message, "", sizeof(messages[i].message) - 1);
        messages[i].width = 0;
    }

    int message_count = 0;
    int current_message_index = 0;
    for (int i = 0; i < word_count; i++)
    {
        int potential_width = messages[current_message_index].width + words[i].width;
        if (i > 0)
        {
            potential_width += letter_width;
        }
        if (potential_width <= FIXED_WIDTH - 2 * message_padding)
        {
            if (messages[current_message_index].width == 0)
            {
                strncpy(messages[current_message_index].message, words[i].message, sizeof(words[i].message) - 1);
            }
            else
            {
                char messageBuf[256];
                snprintf(messageBuf, sizeof(messageBuf), "%s %s", messages[current_message_index].message, words[i].message);

                strncpy(messages[current_message_index].message, messageBuf, sizeof(messageBuf) - 1);
            }
            messages[current_message_index].width += words[i].width;
        }
        else
        {
            current_message_index++;
            message_count++;
            strncpy(messages[current_message_index].message, words[i].message, sizeof(words[i].message) - 1);
            messages[current_message_index].width = words[i].width;
        }
    }

    int messages_height = (current_message_index + 1) * word_height + (SCALE1(PADDING) * current_message_index);
    // default to the middle of the screen
    int current_message_y = (screen->h - messages_height) / 2;
    if (display_state->message_alignment == MessageAlignmentTop)
    {
        current_message_y = SCALE1(PADDING);
    }
    else if (display_state->message_alignment == MessageAlignmentBottom)
    {
        current_message_y = screen->h - messages_height - SCALE1(PADDING);
    }
    for (int i = 0; i <= message_count; i++)
    {
        char *message = messages[i].message;
        int width = messages[i].width;

        SDL_Surface *text = TTF_RenderUTF8_Blended(state->fonts.large, message, COLOR_WHITE);
        SDL_Rect pos = {
            ((screen->w - text->w) / 2),
            current_message_y,
            text->w,
            text->h};
        SDL_BlitSurface(text, NULL, screen, &pos);
        current_message_y += word_height + SCALE1(PADDING);
    }

    if (strcmp(state->action_button, "") != 0)
    {
        GFX_blitButtonGroup((char *[]){state->action_button, state->action_text, NULL}, 0, screen, 0);
    }

    // don't forget to reset the should_redraw flag
    state->redraw = 0;
}

void signal_handler(int signal)
{
    // if the signal is a ctrl+c, exit with code 130
    if (signal == SIGINT)
    {
        exit(ExitCodeKeyboardInterrupt);
    }
    else
    {
        exit(ExitCodeError);
    }
}

// parse_arguments parses the arguments using getopt and updates the app state
// supports the following flags:
// - --action-button <button> (default: "")
// - --action-text <text> (default: "ACTION")
// - --action-show (default: false)
// - --confirm-button <button> (default: "A")
// - --confirm-text <text> (default: "SELECT")
// - --confirm-show (default: false)
// - --cancel-button <button> (default: "B")
// - --cancel-text <text> (default: "BACK")
// - --cancel-show (default: false)
// - --timeout <seconds> (default: 1)
// - --message <message> (default: empty string)
// - --font <path> (default: empty string)
// - --font-size <size> (default: FONT_LARGE)
// - --show-hardware-group (default: 0)
// - --show-time-left (default: false)
bool parse_arguments(struct AppState *state, int argc, char *argv[])
{
    static struct option long_options[] = {
        {"action-button", required_argument, 0, 'a'},
        {"action-text", required_argument, 0, 'A'},
        {"action-show", no_argument, 0, 'Y'},
        {"confirm-button", required_argument, 0, 'b'},
        {"confirm-text", required_argument, 0, 'c'},
        {"confirm-show", no_argument, 0, 'X'},
        {"cancel-button", required_argument, 0, 'B'},
        {"cancel-text", required_argument, 0, 'C'},
        {"cancel-show", no_argument, 0, 'Z'},
        {"message", required_argument, 0, 'm'},
        {"font-default", required_argument, 0, 'f'},
        {"font-size-default", required_argument, 0, 'F'},
        {"show-hardware-group", no_argument, 0, 'S'},
        {"show-time-left", no_argument, 0, 'T'},
        {"timeout", required_argument, 0, 't'},
        {0, 0, 0, 0}};

    int opt;
    char *font_path = NULL;
    char message[1024];
    while ((opt = getopt_long(argc, argv, "a:A:b:c:B:C:d:m:f:F:S:TYXZ", long_options, NULL)) != -1)
    {
        switch (opt)
        {
        case 'a':
            strncpy(state->action_button, optarg, sizeof(state->action_button) - 1);
            break;
        case 'A':
            strncpy(state->action_text, optarg, sizeof(state->action_text) - 1);
            break;
        case 'b':
            strncpy(state->confirm_button, optarg, sizeof(state->confirm_button) - 1);
            break;
        case 'B':
            strncpy(state->cancel_button, optarg, sizeof(state->cancel_button) - 1);
            break;
        case 'c':
            strncpy(state->confirm_text, optarg, sizeof(state->confirm_text) - 1);
            break;
        case 'C':
            strncpy(state->cancel_text, optarg, sizeof(state->cancel_text) - 1);
            break;
        case 'f':
            font_path = optarg;
            break;
        case 'F':
            state->fonts.size = atoi(optarg);
            break;
        case 'm':
            strncpy(message, optarg, sizeof(message) - 1);
            break;
        case 't':
            state->timeout_seconds = atoi(optarg);
            break;
        case 'S':
            state->show_hardware_group = 1;
            break;
        case 'T':
            state->show_time_left = true;
            break;
        case 'Y':
            state->action_show = true;
            break;
        case 'X':
            state->confirm_show = true;
            break;
        case 'Z':
            state->cancel_show = true;
            break;
        default:
            return false;
        }
    }

    if (message != NULL)
    {
        strncpy(state->display_states[0].message, message, sizeof(message) - 1);
        strncpy(state->display_states[0].background_image, "", 0);
        state->display_states[0].background_color = 0;
        state->display_states[0].message_alignment = MessageAlignmentMiddle;
    }

    if (font_path != NULL)
    {
        // check if the font path is valid
        if (access(font_path, F_OK) == -1)
        {
            log_error("Invalid font path provided");
            return false;
        }
        state->fonts.large = TTF_OpenFont(font_path, SCALE1(state->fonts.size));
        if (state->fonts.large == NULL)
        {
            log_error("Failed to open large font");
            return false;
        }
        TTF_SetFontStyle(state->fonts.large, TTF_STYLE_BOLD);

        state->fonts.small = TTF_OpenFont(font_path, SCALE1(FONT_SMALL));
        if (state->fonts.small == NULL)
        {
            log_error("Failed to open small font");
            return false;
        }
    }
    else
    {
        state->fonts.large = TTF_OpenFont(FONT_PATH, SCALE1(state->fonts.size));
        if (state->fonts.large == NULL)
        {
            log_error("Failed to open large font");
            return false;
        }
        TTF_SetFontStyle(state->fonts.large, TTF_STYLE_BOLD);

        state->fonts.small = TTF_OpenFont(FONT_PATH, SCALE1(FONT_SMALL));
        if (state->fonts.small == NULL)
        {
            log_error("Failed to open small font");
            return false;
        }
    }

    // Apply default values for certain buttons and texts
    if (strcmp(state->action_button, "") == 0)
    {
        strncpy(state->action_button, "", sizeof(state->action_button) - 1);
    }

    if (strcmp(state->action_text, "") == 0)
    {
        strncpy(state->action_text, "ACTION", sizeof(state->action_text) - 1);
    }

    if (strcmp(state->cancel_button, "") == 0)
    {
        strncpy(state->cancel_button, "B", sizeof(state->cancel_button) - 1);
    }

    if (strcmp(state->confirm_text, "") == 0)
    {
        strncpy(state->confirm_text, "SELECT", sizeof(state->confirm_text) - 1);
    }

    if (strcmp(state->cancel_text, "") == 0)
    {
        strncpy(state->cancel_text, "BACK", sizeof(state->cancel_text) - 1);
    }

    // validate that hardware buttons aren't assigned to more than once
    bool a_button_assigned = false;
    bool b_button_assigned = false;
    bool x_button_assigned = false;
    bool y_button_assigned = false;
    if (strcmp(state->action_button, "A") == 0)
    {
        a_button_assigned = true;
    }
    if (strcmp(state->cancel_button, "A") == 0)
    {
        if (a_button_assigned)
        {
            log_error("A button cannot be assigned to more than one button");
            return false;
        }

        a_button_assigned = true;
    }
    if (strcmp(state->confirm_button, "A") == 0)
    {
        if (a_button_assigned)
        {
            log_error("A button cannot be assigned to more than one button");
            return false;
        }

        a_button_assigned = true;
    }

    if (strcmp(state->action_button, "B") == 0)
    {
        b_button_assigned = true;
    }
    if (strcmp(state->cancel_button, "B") == 0)
    {
        if (b_button_assigned)
        {
            log_error("B button cannot be assigned to more than one button");
            return false;
        }

        b_button_assigned = true;
    }
    if (strcmp(state->confirm_button, "B") == 0)
    {
        if (b_button_assigned)
        {
            log_error("B button cannot be assigned to more than one button");
            return false;
        }

        b_button_assigned = true;
    }

    if (strcmp(state->action_button, "X") == 0)
    {
        x_button_assigned = true;
    }
    if (strcmp(state->cancel_button, "X") == 0)
    {
        if (x_button_assigned)
        {
            log_error("X button cannot be assigned to more than one button");
            return false;
        }

        x_button_assigned = true;
    }
    if (strcmp(state->confirm_button, "X") == 0)
    {
        if (x_button_assigned)
        {
            log_error("X button cannot be assigned to more than one button");
            return false;
        }

        x_button_assigned = true;
    }

    if (strcmp(state->action_button, "Y") == 0)
    {
        y_button_assigned = true;
    }
    if (strcmp(state->cancel_button, "Y") == 0)
    {
        if (y_button_assigned)
        {
            log_error("Y button cannot be assigned to more than one button");
            return false;
        }
        y_button_assigned = true;
    }
    if (strcmp(state->confirm_button, "Y") == 0)
    {
        if (y_button_assigned)
        {
            log_error("Y button cannot be assigned to more than one button");
            return false;
        }
        y_button_assigned = true;
    }

    // validate that the confirm and cancel buttons are valid
    if (strcmp(state->confirm_button, "A") != 0 && strcmp(state->confirm_button, "B") != 0 && strcmp(state->confirm_button, "X") != 0 && strcmp(state->confirm_button, "Y") != 0)
    {
        log_error("Invalid confirm button provided");
        return false;
    }
    if (strcmp(state->cancel_button, "A") != 0 && strcmp(state->cancel_button, "B") != 0 && strcmp(state->cancel_button, "X") != 0 && strcmp(state->cancel_button, "Y") != 0)
    {
        log_error("Invalid cancel button provided");
        return false;
    }

    if (strlen(message) == 0)
    {
        log_error("No message provided");
        return false;
    }

    return true;
}

// swallow_stdout_from_function swallows stdout from a function
// this is useful for suppressing output from a function
// that we don't want to see in the log file
// the InitSettings() function is an example of this (some implementations print to stdout)
void swallow_stdout_from_function(void (*func)(void))
{
    int original_stdout = dup(STDOUT_FILENO);
    int dev_null = open("/dev/null", O_WRONLY);

    dup2(dev_null, STDOUT_FILENO);
    close(dev_null);

    func();

    dup2(original_stdout, STDOUT_FILENO);
    close(original_stdout);
}

// init initializes the app state
// everything is placed here as MinUI sometimes logs to stdout
// and the logging happens depending on the platform
void init()
{
    // set the cpu speed to the menu speed
    // this is done here to ensure we downclock
    // the menu (no need to draw power unnecessarily)
    PWR_setCPUSpeed(CPU_SPEED_MENU);

    // initialize:
    // - the screen, allowing us to draw to it
    // - input from the pad/joystick/buttons/etc.
    // - power management
    // - sync hardware settings (brightness, hdmi, speaker, etc.)
    if (screen == NULL)
    {
        screen = GFX_init(MODE_MAIN);
    }
    PAD_init();
    PWR_init();
    InitSettings();
}

// destruct cleans up the app state in reverse order
void destruct()
{
    QuitSettings();
    PWR_quit();
    PAD_quit();
    GFX_quit();
}

// main is the entry point for the app
int main(int argc, char *argv[])
{
    swallow_stdout_from_function(init);

    signal(SIGINT, signal_handler);

    // Initialize app state
    char default_action_button[1024] = "";
    char default_action_text[1024] = "ACTION";
    char default_cancel_button[1024] = "B";
    char default_cancel_text[1024] = "BACK";
    char default_confirm_button[1024] = "A";
    char default_confirm_text[1024] = "SELECT";
    char default_message[1024] = "";
    struct AppState state = {
        .redraw = 1,
        .quitting = 0,
        .exit_code = EXIT_SUCCESS,
        .show_hardware_group = 0,
        .timeout_seconds = -1,
        .fonts = {
            .size = FONT_LARGE,
            .large = NULL,
            .medium = NULL,
        },
        .action_show = false,
        .confirm_show = false,
        .cancel_show = false,
        .show_time_left = false,
        .current_display_state_index = 0,
        .start_time = 0,
        .display_states = {},
    };

    // assign the default values to the app state
    strncpy(state.action_button, default_action_button, sizeof(state.action_button) - 1);
    strncpy(state.action_text, default_action_text, sizeof(state.action_text) - 1);
    strncpy(state.cancel_button, default_cancel_button, sizeof(state.cancel_button) - 1);
    strncpy(state.cancel_text, default_cancel_text, sizeof(state.cancel_text) - 1);
    strncpy(state.confirm_button, default_confirm_button, sizeof(state.confirm_button) - 1);
    strncpy(state.confirm_text, default_confirm_text, sizeof(state.confirm_text) - 1);

    // parse the arguments
    if (!parse_arguments(&state, argc, argv))
    {
        return ExitCodeError;
    }

    // get initial wifi state
    int was_online = PLAT_isOnline();

    // get the current time
    gettimeofday(&state.start_time, NULL);

    int show_setting = 0; // 1=brightness,2=volume

    while (!state.quitting)
    {
        // start the frame to ensure GFX_sync() works
        // on devices that don't support vsync
        GFX_startFrame();

        // handle turning the on/off screen on/off
        // as well as general power management
        PWR_update(&state.redraw, &show_setting, NULL, NULL);

        // check if the device is on wifi
        // redraw if the wifi state changed
        // and then update our state
        int is_online = PLAT_isOnline();
        if (was_online != is_online)
        {
            state.redraw = 1;
        }
        was_online = is_online;

        // handle any input events
        handle_input(&state);

        // redraw the screen if there has been a change
        if (state.redraw)
        {
            // clear the screen at the beginning of each loop
            GFX_clear(screen);

            if (state.show_hardware_group)
            {
                // draw the hardware information in the top-right
                GFX_blitHardwareGroup(screen, show_setting);
                // draw the setting hints
                if (show_setting && !GetHDMI())
                {
                    GFX_blitHardwareHints(screen, show_setting);
                }
                else
                {
                    GFX_blitButtonGroup((char *[]){BTN_SLEEP == BTN_POWER ? "POWER" : "MENU", "SLEEP", NULL}, 0, screen, 0);
                }
            }

            // your draw logic goes here
            draw_screen(screen, &state);

            // Takes the screen buffer and displays it on the screen
            GFX_flip(screen);
        }
        else
        {
            // Slows down the frame rate to match the refresh rate of the screen
            // when the screen is not being redrawn
            GFX_sync();
        }

        // if the sleep seconds is larger than 0, check if the sleep has expired
        if (state.timeout_seconds > 0)
        {
            struct timeval current_time;
            gettimeofday(&current_time, NULL);
            if (current_time.tv_sec - state.start_time.tv_sec >= state.timeout_seconds)
            {
                state.exit_code = ExitCodeTimeout;
                state.quitting = 1;
            }

            if (current_time.tv_sec != state.start_time.tv_sec)
            {
                state.redraw = 1;
            }
        }
    }

    swallow_stdout_from_function(destruct);

    // exit the program
    return state.exit_code;
}
