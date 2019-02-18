#include "morse.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>

#ifdef MORSE_MAIN
  #include <unistd.h>
#endif  // MORSE_MAIN

#define OFFSET 48
#define MAX_CHAR 'Z'


// --------------------------------------------------------------------


void (*morse_cb)(bool value);

static uint8_t morse_buf1[MORSE_MAX_LEN];
static uint8_t morse_buf2[MORSE_MAX_LEN];
static uint8_t *morse_live_buf = morse_buf1;
static uint8_t *morse_next_buf = morse_buf2;
static bool morse_repeat = false;
static bool morse_repeat_next = false;
static uint32_t morse_dot_duration = 60;  // ms  ~= 10wpm
static uint32_t morse_elapsed_time = 0;  // time since message start.

static char *letters[] = {
              // i      ch      a
    "-----",  // 0      0       48
    ".----",  // 1      1       49
    "..---",  // 2      2       50
    "...--",  // 3      3       51
    "....-",  // 4      4       52
    ".....",  // 5      5       53
    "-....",  // 6      6       54
    "--...",  // 7      7       55
    "---..",  // 8      8       56
    "----.",  // 9      9       57
    "",       // 10             58
    "",       // 11             59
    "",       // 12             60
    "",       // 13             61
    "",       // 14             62
    "",       // 15             63
    "",       // 16             64
    ".-",     // 17     A       65
    "-...",   // 18     B       66
    "-.-.",   // 19     C       67
    "-..",    // 20     D       68
    ".",      // 21     E       69
    "..-.",   // 22     F       70
    "--.",    // 23     G       71
    "....",   // 24     H       72
    "..",     // 25     I       73
    ".---",   // 26     J       74
    "-.-",    // 27     K       75
    ".-..",   // 28     L       76
    "--",     // 29     M       77
    "-.",     // 30     N       78
    "---",    // 31     O       79
    ".--.",   // 32     P       80
    "--.-",   // 33     Q       81
    ".-.",    // 34     R       82
    "...",    // 35     S       83
    "-",      // 36     T       84
    "..-",    // 37     U       85
    "...-",   // 38     V       86
    ".--",    // 39     W       87
    "-..-",   // 40     X       88
    "-.--",   // 41     Y       89
    "--..",   // 42     Z       90
};


// --------------------------------------------------------------------


static void morse_switch_buf(void);
static bool morse_encode(uint8_t *buf, const char *s, const uint32_t size);
static void morse_encode_len(uint8_t *buf, const uint32_t bit_len);

static uint8_t morse_encode_char(
        uint8_t *buf, char c, uint32_t index, uint32_t limit);

static bool morse_encode_bit(
        uint8_t *buf, bool value, uint8_t n, uint32_t *index, uint32_t limit);

static bool morse_bit(const uint8_t *buf, const uint32_t bit_index);
static uint32_t morse_len(const uint8_t *buf);


// --------------------------------------------------------------------


/**
 * Set a string to be played after the current string is complete, or
 * immediately if no current string is being transmitted.
 * 
 * If a string is already live and repeating, it will finish the
 * current iteration and then stop, after which the passed string
 * will begin.
 * 
 * If string is set to repeat it will repeat until a new message is set
 * or morse_stop() or morse_interrupt() is called.
 * 
 * \param s String to transmit. 
 * \param bool Whether string should be repeated.
 */
void morse(const char *s, bool repeat) {
    morse_encode(morse_next_buf, s, MORSE_MAX_LEN);
    morse_repeat_next = repeat;
    morse_repeat = false;
}

/**
 * Updates morse state, intended to be called regularly.
 * 
 * \param elapsed_ms time since last update was called.
 */
void morse_update(uint32_t elapsed_ms) {
    // If nothing is in the live buffer, return.
    const uint32_t live_len = morse_len(morse_live_buf);
    if (!live_len && !morse_len(morse_next_buf)) return;
    morse_elapsed_time += elapsed_ms;
    const uint32_t bit_index = morse_elapsed_time / morse_dot_duration;

    // If bit index is past end of used buffer, switch to next buf.
    if (bit_index >= live_len) {
        if (!morse_repeat) morse_switch_buf();
        morse_elapsed_time = 0;
    }

    // Set current signal.
    morse_cb(morse_bit(morse_live_buf, bit_index));
}

/**
 * \brief Stop currently playing string after the current iteration.
 * 
 * Also clears the next string if one is set.
 */
void morse_stop(void) {
    morse_repeat = false;
}

/**
 * \brief Interrupt the currently playing string immediately.
 */
void morse_interrupt(void) {
    morse_encode_len(morse_live_buf, 0);
    morse_repeat = false;
}


// --------------------------------------------------------------------


static void morse_switch_buf(void) {
    if (morse_live_buf == morse_buf1) {
        morse_live_buf = morse_buf2;
        morse_next_buf = morse_buf1;
    } else {
        morse_live_buf = morse_buf1;
        morse_next_buf = morse_buf2;
    }
    morse_repeat = morse_repeat_next;
    morse_repeat_next = false;
    morse_encode_len(morse_next_buf, 0);  // Clear buf.
}

static bool morse_encode(uint8_t *buf, const char *s, const uint32_t size) {
    if (size < 4) {
        fprintf(stderr, "Buffer must have size of >= 5.");
        return false;
    }
    uint32_t i = 32;  // Skip length bytes.
    const uint32_t bit_size = size * 8;
    for (const char *c = s; *c != '\0'; ++c) {
        uint32_t char_len = morse_encode_char(buf, *c, i, bit_size);
        if (!char_len) return false;
        i += char_len;
    }
    // Add padding to message end to help separate messages.
    for (uint8_t j = 0; j < 3; ++j) {
        uint32_t char_len = morse_encode_char(buf, ' ', i, bit_size);
        if (!char_len) return false;
        i += char_len;
    }
    morse_encode_len(buf, i - 32);
    return true;
}

static void morse_encode_len(uint8_t *buf, const uint32_t bit_len) {
    buf[0] = (bit_len & 0xFF000000) >> 24;
    buf[1] = (bit_len & 0x00FF0000) >> 16;
    buf[2] = (bit_len & 0x0000FF00) >> 8;
    buf[3] = (bit_len & 0x000000FF);
}

static uint8_t morse_encode_char(
        uint8_t *buf,
        const char ch,
        const uint32_t index,
        const uint32_t limit) {
    uint32_t i = index;
    const char c = toupper(ch);
    // If c is a space; add four dot durations of silence.
    // Combined with the two spaces at the start of a character,
    // and the one space at the start of each element, forms the 7
    // dot durations of separation that are expected between words.
    if (c == ' ') {
        if (!morse_encode_bit(buf, 0, 4, &i, limit)) return 0;
    } else {
        if (c < OFFSET || c > MAX_CHAR) {
            fprintf(stderr, "Invalid char: %c", ch);
            return 0;
        }
        // Append two empty dot durations at the start of a character.
        // Combines with empty dot duration at start of element to form
        // the expected three-dot 'off' period at the start of a char.
        if (!morse_encode_bit(buf, 0, 2, &i, limit)) return 0;
        const char * const series = letters[c - OFFSET];
        for (const char *element = series; *element != '\0'; ++element) {
            // Add empty dot duration as spacing at start of element.
            if (!morse_encode_bit(buf, 0, 1, &i, limit)) return 0;
            if (*element == '.') {
                if (!morse_encode_bit(buf, 1, 1, &i, limit)) return 0;
            } else {
                if (!morse_encode_bit(buf, 1, 3, &i, limit)) return 0;
            }
        }
    }
    return i - index;
}

/**
 * \brief Encode a bit indicating signal on or off.
 * 
 * This is assumed to be called sequentially on bits, And as writing
 * the first bit of a byte will clear the entire byte, writing bits
 * out-of-order will probably not result in expected behavior.
 */
static bool morse_encode_bit(
        uint8_t *buf,
        const bool value,  // 0 or 1
        const uint8_t n,
        uint32_t *index,
        const uint32_t limit) {
    for (uint32_t i = *index; i < *index + n; ++i) {
        if (i >= limit) {
            fprintf(stderr, "Buffer size limit reached.");
            buf[0] = buf[1] = buf[2] = buf[3] = 0;
            return false;
        }

        uint8_t bit_index = i % 8;
        uint32_t buf_index = i / 8;
        // If this is the first bit of the byte,
        // clear the whole byte.
        if (!bit_index) buf[buf_index] = 0;
        // Set bit
        buf[buf_index] |= value << bit_index;
    }
    *index += n;
    return true;
}

static bool morse_bit(const uint8_t *buf, const uint32_t bit_index) {
    const uint32_t i = bit_index + 32;
    return (buf[i / 8] >> (i % 8)) & 1;
}

/**
 * \brief Get length in bits / dot durations of encoded morse message.
 */
static uint32_t morse_len(const uint8_t *buf) {
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

// --------------------------------------------------------------------


#ifdef MORSE_MAIN

static void morse_console(bool value) {
    printf(value ? "\r    XXX    " : "\r           ");
    fflush(stdout);
}

int main() {
    morse("Hello CQ DE Morse", true);
    morse_cb = morse_console;
    while (1) {
        morse_update(10);
        usleep(10 * 1000);
    }
}


#endif  // MORSE_MAIN
