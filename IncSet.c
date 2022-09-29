/*
htop - IncSet.c
(C) 2005-2012 Hisham H. Muhammad
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "IncSet.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include <pcre2.h>

#include "CRT.h"
#include "ListItem.h"
#include "Object.h"
#include "ProvideCurses.h"
#include "XUtils.h"

// WARN: remove if not used
#define DEBUG_REGEX 0


static void IncRegex_reset(IncRegex* re) {
   if (re && re->code) {
      pcre2_code_free(re->code);
      re->code = NULL;
   }
}

static void IncMode_reset(IncMode* mode) {
   mode->index = 0;
   mode->buffer[0] = 0;
   IncRegex_reset(mode->re);
}

void IncSet_reset(IncSet* this, IncType type) {
   IncMode_reset(&this->modes[type]);
}

// TODO: this is only used in one location CommandLine.c
void IncSet_setFilter(IncSet* this, const char* filter) {
   IncMode* mode = &this->modes[INC_FILTER];
   size_t len = String_safeStrncpy(mode->buffer, filter, sizeof(mode->buffer));
   mode->index = len;
   this->filtering = true;
}

static const char* const searchFunctions[] = {"Next  ", "Prev   ", "Cancel ", " Search: ", NULL};
static const char* const searchKeys[] = {"F3", "S-F3", "Esc", "  "};
static const int searchEvents[] = {KEY_F(3), KEY_F(15), 27, ERR};

static inline bool IncRegex_match(const IncRegex* re, const char *subject) {
   const PCRE2_SPTR s = (const PCRE2_SPTR)subject;
   int ret = pcre2_match(re->code, s, PCRE2_ZERO_TERMINATED,
                         0, 0 /* options */, re->match_data, re->context);
#if DEBUG_REGEX
   CRT_debug("pcre2_match(\"%s\") = %d\n", subject, ret);
#endif
   if (ret > 0) {
      return true;
   }
   if (ret <= 0 && ret != PCRE2_ERROR_NOMATCH) {
      // WARN: error
   }
   return false;
}

static inline void IncRegex_delete(IncRegex* re) {
   if (re) {
      if (re->code) {
         pcre2_code_free(re->code);
      }
      if (re->match_data) {
         pcre2_match_data_free(re->match_data);
      }
      if (re->context) {
         pcre2_match_context_free(re->context);
      }
      if (re->jit_stack) {
         pcre2_jit_stack_free(re->jit_stack);
      }
#ifndef NDEBUG
      *re = (IncRegex){ 0 };
#endif
      free(re);
   }
}

static void IncMode_initRegex(IncMode* inc) {
   inc->re = xCalloc(1, sizeof(IncRegex));
}

// WARN: move
static inline bool IncMode_hasRe(const IncMode* this) {
   return this->re != NULL && this->re->code != NULL;
}

// WARN: move
bool IncMode_match(const IncMode* this, const char* str) {
   return IncMode_hasRe(this) ? IncRegex_match(this->re, str) :
      String_contains_i(str, this->buffer, true);
}

// WARN: move
static bool IncSet_match(const IncSet* this, const char* str, IncType mode) {
   assert(INC_SEARCH <= mode && mode <= INC_FILTER);
   return IncMode_match(&this->modes[mode], str);
}


static inline void IncMode_initSearch(IncMode* search) {
   memset(search, 0, sizeof(IncMode));
   search->bar = FunctionBar_new(searchFunctions, searchKeys, searchEvents);
   search->isFilter = false;
   IncMode_initRegex(search);
}

static const char* const filterFunctions[] = {"Done  ", "Clear ", " Filter: ", NULL};
static const char* const filterKeys[] = {"Enter", "Esc", "  "};
static const int filterEvents[] = {13, 27, ERR};

static inline void IncMode_initFilter(IncMode* filter) {
   memset(filter, 0, sizeof(IncMode));
   filter->bar = FunctionBar_new(filterFunctions, filterKeys, filterEvents);
   filter->isFilter = true;
   IncMode_initRegex(filter);
}

static inline void IncMode_done(IncMode* mode) {
   FunctionBar_delete(mode->bar);
   IncRegex_delete(mode->re);
}

IncSet* IncSet_new(FunctionBar* bar) {
   IncSet* this = xMalloc(sizeof(IncSet));
   IncMode_initSearch(&(this->modes[INC_SEARCH]));
   IncMode_initFilter(&(this->modes[INC_FILTER]));
   this->active = NULL;
   this->defaultBar = bar;
   this->filtering = false;
   this->found = false;
   return this;
}

void IncSet_delete(IncSet* this) {
   IncMode_done(&(this->modes[0]));
   IncMode_done(&(this->modes[1]));
   free(this);
}

static void updateWeakPanel(const IncSet* this, Panel* panel, Vector* lines) {
   const Object* selected = Panel_getSelected(panel);
   Panel_prune(panel);
   if (this->filtering) {
      int n = 0;
      // const char* incFilter = this->modes[INC_FILTER].buffer;
      for (int i = 0; i < Vector_size(lines); i++) {
         ListItem* line = (ListItem*)Vector_get(lines, i);
         // if (String_contains_i(line->value, incFilter, true)) {
         if (IncSet_match(this, line->value, INC_FILTER)) {
            Panel_add(panel, (Object*)line);
            if (selected == (Object*)line) {
               Panel_setSelected(panel, n);
            }

            n++;
         }
      }
   } else {
      for (int i = 0; i < Vector_size(lines); i++) {
         Object* line = Vector_get(lines, i);
         Panel_add(panel, line);
         if (selected == line) {
            Panel_setSelected(panel, i);
         }
      }
   }
}

static bool search(const IncSet* this, Panel* panel, IncMode_GetPanelValue getPanelValue) {
   int size = Panel_size(panel);
   for (int i = 0; i < size; i++) {
      if (IncMode_match(this->active, getPanelValue(panel, i))) {
         Panel_setSelected(panel, i);
         return true;
      }
   }

   return false;
}

void IncSet_activate(IncSet* this, IncType type, Panel* panel) {
   this->active = &(this->modes[type]);
   panel->currentBar = this->active->bar;
   panel->cursorOn = true;
   this->panel = panel;
   IncSet_drawBar(this, CRT_colors[FUNCTION_BAR]);
}

static void IncSet_deactivate(IncSet* this, Panel* panel) {
   this->active = NULL;
   Panel_setDefaultBar(panel);
   panel->cursorOn = false;
   FunctionBar_draw(this->defaultBar);
}

static bool IncMode_find(const IncMode* mode, Panel* panel,
   IncMode_GetPanelValue getPanelValue, int step) {

   int size = Panel_size(panel);
   int here = Panel_getSelectedIndex(panel);
   int i = here;
   for (;;) {
      i += step;
      if (i == size) {
         i = 0;
      }
      if (i == -1) {
         i = size - 1;
      }
      if (i == here) {
         return false;
      }

      if (IncMode_match(mode, getPanelValue(panel, i))) {
         Panel_setSelected(panel, i);
         return true;
      }
   }
}

static bool non_ascii(int ch) {
   return !isascii(ch) || ch == '\033';
}

static bool has_non_ascii(const char *s) {
   if (!s) {
      return false;
   }
   int ch;
   while ((ch = *s++) != '\0') {
      if (non_ascii(ch)) {
         return true;
      }
   }
   return false;
}

static int IncRegex_initJitStack(IncRegex *re) {
   assert(re && re->code);
   if (!re->jit_stack) {
      // The context should be NULL if the jit_stack is NULL.
      assert(re->context == NULL);

      re->context = pcre2_match_context_create(NULL);
      re->jit_stack = pcre2_jit_stack_create(32*1024, 512*1024, NULL);
      assert(re->context);
      assert(re->jit_stack);
      if (!re->jit_stack || !re->context) {
         return 1;
      }
      // TODO: just check the jit_stack and bail on error
      pcre2_jit_stack_assign(re->context, NULL, re->jit_stack);
   }
   return 0;
}

static inline bool has_meta(const char *s) {
   return s[strcspn(s, "+.*?^$|{}[]()\\")] != '\0';
}

static void IncRegex_compile(IncRegex *re, const char *pattern) {
   if (re->code) {
      pcre2_code_free(re->code);
      re->code = NULL;
   }
   // use strcasestr if the pattern is empty or is not a regex
   if (!pattern || pattern[0] == '\0' || !has_meta(pattern)) {
      return;
   }

   // TODO: check locale as well (see git/lib/localeinfo.c):
   //    multibyte = MB_CUR_MAX > 1;
   //
   uint32_t options = PCRE2_CASELESS | PCRE2_MULTILINE;
   if (has_non_ascii(pattern)) {
      options |= PCRE2_UTF;
   }

   int errorcode = 0;
   size_t erroffset = 0;
   assert(re->code == NULL);
   re->code = pcre2_compile((const unsigned char*)pattern,
                            PCRE2_ZERO_TERMINATED, options, &errorcode,
                            &erroffset, NULL);
   if (!re->code) {
      // WARN: handle
      return;
   }

   uint32_t ovecsize = 0;
   if (pcre2_pattern_info(re->code, PCRE2_INFO_CAPTURECOUNT, &ovecsize) != 0) {
      assert(false); // WARN: handle error
   }
   ovecsize++;
   if (ovecsize > re->match_data_ovecsize || re->match_data == NULL) {
      if (re->match_data) {
         pcre2_match_data_free(re->match_data);
      }
      re->match_data = pcre2_match_data_create(ovecsize, NULL);
      re->match_data_ovecsize = ovecsize;
      assert(re->match_data);
   }

   // Lazily initialize the jit stack and match context
   if (IncRegex_initJitStack(re) != 0) {
      // WARN: handle error
      assert(false);
   }
   // TODO: check if JIT is available
   if (pcre2_jit_compile(re->code, PCRE2_JIT_COMPLETE) != 0) {
      // WARN: handle error
      assert(false);
   }
}

bool IncSet_handleKey(IncSet* this, int ch, Panel* panel, IncMode_GetPanelValue getPanelValue, Vector* lines) {
   if (ch == ERR)
      return true;

   IncMode* mode = this->active;
   int size = Panel_size(panel);
   bool filterChanged = false;
   bool doSearch = true;
   int prev_index = mode->index;
   if (ch == KEY_F(3) || ch == KEY_F(15)) {
      if (size == 0)
         return true;

      IncMode_find(mode, panel, getPanelValue, ch == KEY_F(3) ? 1 : -1);
      doSearch = false;
   } else if (0 < ch && ch < 255 && isprint((unsigned char)ch)) {
      if (mode->index < INCMODE_MAX) {
         mode->buffer[mode->index] = (char) ch;
         mode->index++;
         mode->buffer[mode->index] = 0;
         if (mode->isFilter) {
            filterChanged = true;
            if (mode->index == 1) {
               this->filtering = true;
            }
         }
      }
   } else if (ch == KEY_BACKSPACE || ch == 127) {
      if (mode->index > 0) {
         mode->index--;
         mode->buffer[mode->index] = 0;
         if (mode->isFilter) {
            filterChanged = true;
            if (mode->index == 0) {
               this->filtering = false;
               IncMode_reset(mode);
            }
         }
      } else {
         doSearch = false;
      }
   } else if (ch == KEY_RESIZE) {
      doSearch = (mode->index > 0);
   } else {
      if (mode->isFilter) {
         filterChanged = true;
         if (ch == 27) {
            this->filtering = false;
            IncMode_reset(mode);
         }
      } else {
         if (ch == 27) {
            IncMode_reset(mode);
         }
      }
      IncSet_deactivate(this, panel);
      doSearch = false;
   }
   if (prev_index != mode->index) {
      // WARN: handle errors
      IncRegex_compile(mode->re, mode->buffer);
   }
   if (doSearch) {
      this->found = search(this, panel, getPanelValue);
   }
   if (filterChanged && lines) {
      updateWeakPanel(this, panel, lines);
   }
   return filterChanged;
}

const char* IncSet_getListItemValue(Panel* panel, int i) {
   const ListItem* l = (const ListItem*) Panel_get(panel, i);
   return l ? l->value : "";
}

void IncSet_drawBar(const IncSet* this, int attr) {
   if (this->active) {
      if (!this->active->isFilter && !this->found)
         attr = CRT_colors[FAILED_SEARCH];
      int cursorX = FunctionBar_drawExtra(this->active->bar, this->active->buffer, attr, true);
      this->panel->cursorY = LINES - 1;
      this->panel->cursorX = cursorX;
   } else {
      FunctionBar_draw(this->defaultBar);
   }
}

int IncSet_synthesizeEvent(IncSet* this, int x) {
   if (this->active) {
      return FunctionBar_synthesizeEvent(this->active->bar, x);
   } else {
      return FunctionBar_synthesizeEvent(this->defaultBar, x);
   }
}
