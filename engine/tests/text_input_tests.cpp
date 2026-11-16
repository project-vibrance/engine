// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/interactions.h>

#include <iostream>
#include <stdexcept>

namespace
{
    unsigned checks = 0u;
    void expect(bool value, const std::string& description)
    {
        ++checks;
        if (!value) throw std::runtime_error(description);
    }
    std::string utf8(std::u8string_view value)
    { return { reinterpret_cast<const char*>(value.data()), value.size() }; }

    UiKeyMap key_map()
    {
        UiKeyMap keys;
        keys.escape = 1; keys.backspace = 2; keys.deleteKey = 3; keys.enter = 4;
        keys.paste = 5; keys.clearLine = 6; keys.left = 7; keys.right = 8;
        keys.up = 9; keys.down = 10; keys.home = 11; keys.end = 12;
        keys.pageUp = 13; keys.pageDown = 14; keys.selectAll = 15;
        keys.copy = 16; keys.cut = 17; keys.undo = 18; keys.redo = 19;
        keys.insert = 20; keys.tab = 21; keys.killLine = 22;
        return keys;
    }
    struct Fixture
    {
        Renderer2DScene scene;
        Renderer2DFontAtlas atlas;
        UiBuilder ui { scene };
        UiInputState state;
        UiKeyMap keys = key_map();
        UiControlHandle field;
        std::string clipboard;
        unsigned changes = 0u, submissions = 0u;
        double clock = 1.0;
        Fixture(std::string value = "first second", glm::vec2 size = { 240.0f, 30.0f })
        {
            UiSearchFieldOptions options;
            options.onChanged = [&](const std::string&) { ++changes; };
            options.onSubmitted = [&](const std::string&) { ++submissions; };
            field = ui_create_search_field(ui, atlas, ui.root(), UiAlignment::eTopLeft, { 20.0f, 20.0f }, size, 5, 0u, options);
            input().value = std::move(value);
            ui_focus_text_input(scene, atlas, state, field.root);
            layout();
        }
        TextInputComponent& input() { return scene.registry().get<TextInputComponent>(field.root); }
        void layout() { scene.build_render_plan(clock, 0u); }
        void set(std::string value, std::size_t caret = std::string::npos)
        {
            input().value = std::move(value);
            ui_text_input_sync(input());
            ui_text_move_caret(input(), caret, false);
            ui_update_text_input_visual(scene, atlas, field.root);
            layout();
        }
        bool key(int code, bool control = false, bool shift = false, bool alt = false, bool super = false,
            UiInputAction action = UiInputAction::ePress)
        {
            UiKeyInput event;
            event.key = code; event.keys = keys; event.action = action;
            event.modifiers = { shift, control, alt, super };
            event.clipboardText = clipboard;
            event.writeClipboard = [&](const std::string& value) { clipboard = value; };
            event.timeSeconds = clock += 0.05;
            return ui_handle_key(scene, atlas, state, event);
        }
        void type(uint32_t codepoint) { ui_append_text_input(scene, atlas, state, codepoint, clock += 0.05); }
        std::string selected()
        {
            const auto range = ui_text_selection(input());
            return input().value.substr(range.start, range.end - range.start);
        }
        glm::vec2 point(float character)
        {
            layout();
            const auto rect = ui_entity_framebuffer_rect(scene.registry(), field.label);
            return { rect.x + character * 14.0f * 0.55f, rect.y + rect.w * 0.5f };
        }
        void click(glm::vec2 position, unsigned count = 1u, bool shift = false)
        {
            UiPointerButtonInput event;
            event.hasPoint = true; event.point = position; event.button = PointerButton::eLeft;
            event.modifiers.shift = shift; event.timeSeconds = clock += 0.05; event.clickCount = count;
            ui_handle_pointer_button(scene, atlas, state, event);
            event.action = UiInputAction::eRelease;
            ui_handle_pointer_button(scene, atlas, state, event);
        }
    };
}

int main()
try
{
    {
        Fixture f;
        f.key(f.keys.left, true);
        expect(f.input().caretByte == 6u, "Ctrl+Left must move to the preceding word");
        f.key(f.keys.left, true, true);
        expect(f.selected() == "first ", "Ctrl+Shift+Left must extend from the original anchor");
        f.key(f.keys.right, true, true);
        expect(f.selected().empty() && f.input().caretByte == 6u, "Reversing a selection must return to its unchanged anchor");
        f.key(f.keys.home);
        f.key(f.keys.right, false, true);
        f.key(f.keys.right, false, true);
        expect(f.selected() == "fi", "Shift+Right must select individual characters");
        f.key(f.keys.right);
        expect(f.input().caretByte == 2u && f.selected().empty(), "An unmodified arrow must collapse an existing selection");
        f.key(f.keys.selectAll, true);
        expect(f.selected() == "first second", "Ctrl+A must select the entire value");
        expect(f.changes == 0u, "Movement and selection must not emit value-change callbacks");
        f.type(0xE9u);
        expect(f.input().value == utf8(u8"é") && f.input().caretByte == 2u, "Typing must replace the selection with valid UTF-8");
        f.key(f.keys.undo, true);
        expect(f.input().value == "first second" && f.selected() == "first second", "Undo must restore the value, caret and selection");
        f.key(f.keys.redo, true);
        expect(f.input().value == utf8(u8"é"), "Ctrl+Y must redo the replacement");
        f.key(f.keys.undo, true);
        f.key(f.keys.undo, true, true);
        expect(f.input().value == utf8(u8"é"), "Ctrl+Shift+Z must also redo");
    }
    {
        Fixture f;
        f.key(f.keys.backspace, true);
        expect(f.input().value == "first ", "Ctrl+Backspace must delete the previous word");
        f.key(f.keys.backspace, true);
        expect(f.input().value.empty(), "Word deletion must consume separating whitespace with the preceding word");
        f.set("first second", 0u);
        f.key(f.keys.deleteKey, true);
        expect(f.input().value == "second", "Ctrl+Delete must delete the next word and its separator");
        f.set("abc", 1u);
        f.key(f.keys.deleteKey);
        expect(f.input().value == "ac" && f.input().caretByte == 1u, "Delete must delete forward at the caret");
        f.key(f.keys.backspace);
        expect(f.input().value == "c" && f.input().caretByte == 0u, "Backspace must delete backwards at the caret");
        f.set("one,two", 7u);
        f.key(f.keys.backspace, true);
        expect(f.input().value == "one,", "Word deletion must treat punctuation as a boundary");
        f.set("abc def", 3u);
        f.key(f.keys.killLine, true);
        expect(f.input().value == "abc", "Ctrl+K must delete from the caret to the line end");
        f.key(f.keys.clearLine, true);
        expect(f.input().value.empty(), "Ctrl+U must delete to the line start");
    }
    {
        Fixture f;
        f.key(f.keys.selectAll, true);
        f.key(f.keys.copy, true);
        expect(f.clipboard == "first second" && f.changes == 0u, "Ctrl+C must copy without editing");
        f.key(f.keys.cut, true);
        expect(f.input().value.empty() && f.clipboard == "first second", "Ctrl+X must copy and remove the selection");
        f.clipboard = utf8(u8"café 日本語 👩🏽‍💻");
        f.key(f.keys.paste, true);
        expect(f.input().value == f.clipboard, "Paste must preserve Unicode rather than replacing bytes with question marks");
        f.key(f.keys.selectAll, true);
        f.key(f.keys.insert, true);
        expect(f.clipboard == f.input().value, "Ctrl+Insert must copy the selection");
        f.clipboard = "next\r\nline\tvalue";
        f.key(f.keys.insert, false, true);
        expect(f.input().value == "next line value", "Shift+Insert must paste while sanitising single-line line breaks");
        f.key(f.keys.selectAll, true);
        f.key(f.keys.deleteKey, false, true);
        expect(f.input().value.empty() && f.clipboard == "next line value", "Shift+Delete must cut the selection");
        f.set("readonly");
        f.input().readOnly = true;
        f.key(f.keys.selectAll, true);
        f.key(f.keys.copy, true);
        f.type('x');
        f.key(f.keys.backspace);
        f.key(f.keys.paste, true);
        expect(f.input().value == "readonly" && f.clipboard == "readonly", "Read-only fields must allow copying and selection while rejecting edits");
    }
    for (const auto grapheme : { utf8(u8"é"), utf8(u8"👩🏽‍💻"), utf8(u8"🇬🇧"), utf8(u8"한"), utf8(u8"क्ष") })
    {
        Fixture f("a" + grapheme + "b");
        f.key(f.keys.left);
        f.key(f.keys.backspace);
        expect(f.input().value == "ab" && f.input().caretByte == 1u, "Deletion must preserve combining, emoji, flag and composed-script grapheme boundaries");
        f.key(f.keys.undo, true);
        f.key(f.keys.left);
        expect(f.input().caretByte == 1u, "Arrow movement must not stop inside a displayed grapheme");
    }
    {
        Fixture f("");
        f.input().maxBytes = 4u;
        f.clipboard = utf8(u8"ééé");
        f.key(f.keys.paste, true);
        expect(f.input().value == utf8(u8"éé"), "Byte limits must truncate only at complete grapheme boundaries");
        f.key(f.keys.selectAll, true);
        f.clipboard = utf8(u8"👩🏽‍💻");
        f.key(f.keys.paste, true);
        expect(f.input().value == utf8(u8"éé") && f.selected() == utf8(u8"éé"), "An insertion that cannot fit must not erase the selection or split an emoji");
        f.set("same");
        f.key(f.keys.selectAll, true);
        const auto before = f.changes;
        f.clipboard = "same";
        f.key(f.keys.paste, true);
        expect(f.changes == before && f.selected().empty(), "Replacing selected text with itself must collapse selection without a spurious value callback");
    }
    {
        Fixture f("");
        f.type('a'); f.type('b'); f.type('c');
        f.key(f.keys.undo, true);
        expect(f.input().value.empty(), "Undo must coalesce a continuous typing run");
        f.key(f.keys.redo, true);
        f.key(f.keys.left);
        f.type('X');
        expect(f.input().value == "abXc", "Typing must insert at the caret, not append");
        f.key(f.keys.undo, true);
        expect(f.input().value == "abc" && f.input().caretByte == 2u, "Caret movement must separate undo groups");
        f.key(f.keys.insert);
        f.type('Y');
        expect(f.input().value == "abY", "Insert must toggle overwrite mode");
    }
    {
        Fixture f("first second\n123\nlast line", { 240.0f, 90.0f });
        f.input().multiline = true;
        ui_text_move_caret(f.input(), 4u, false);
        ui_update_text_input_visual(f.scene, f.atlas, f.field.root);
        f.key(f.keys.down);
        expect(f.input().caretByte == 16u, "Down must clamp to the end of a shorter line");
        f.key(f.keys.down);
        expect(f.input().caretByte == 21u, "Vertical movement must retain the preferred horizontal position");
        f.key(f.keys.up, false, true);
        expect(f.input().caretByte == 16u && f.input().selectionAnchorByte == 21u, "Shift+Up must extend vertically from a stable anchor");
        f.key(f.keys.up, false, true);
        expect(f.input().caretByte == 4u && f.input().selectionAnchorByte == 21u, "Repeated vertical selection must retain the original anchor and column");
        f.key(f.keys.end, true, true);
        expect(f.input().caretByte == f.input().value.size() && f.input().selectionAnchorByte == 21u, "Ctrl+Shift+End must extend through the final character");
        f.key(f.keys.home, true, true);
        expect(f.input().caretByte == 0u && f.input().selectionAnchorByte == 21u, "Ctrl+Shift+Home must reverse through the original anchor");
        f.key(f.keys.end, true);
        f.key(f.keys.home);
        expect(f.input().caretByte == 17u, "Home must move to the start of the current line");
        f.key(f.keys.end);
        expect(f.input().caretByte == 26u, "End must move to the end of the current line");
        f.key(f.keys.up, true, true);
        expect(f.input().caretByte == 17u && f.input().selectionAnchorByte == 26u, "Ctrl+Shift+Up must select to the paragraph boundary");
        f.key(f.keys.enter, true);
        expect(f.submissions == 1u, "Ctrl+Enter must submit multiline input without inserting a newline");
        f.key(f.keys.enter);
        expect(f.input().value == "first second\n123\n\n", "Enter must replace the selection with a newline in multiline input");
        f.key(f.keys.selectAll, true);
        f.clipboard = "one\r\ntwo\tthree";
        f.key(f.keys.paste, true);
        expect(f.input().value == "one\ntwo\tthree", "Multiline paste must retain normalised line breaks and tabs");
        f.key(f.keys.selectAll, true);
        unsigned highlights = 0u;
        for (const auto highlight : f.input().selectionEntities)
            highlights += f.scene.registry().get<RenderLayer2DComponent>(highlight).visible;
        expect(highlights == 2u, "Multiline selection must paint each selected line");
        f.key(f.keys.pageUp, true, true);
        expect(f.input().caretByte == 0u && f.input().selectionAnchorByte == 0u,
            "Ctrl+Shift+Page Up must extend to the document start");
        f.key(f.keys.pageDown, true, true);
        expect(f.selected() == f.input().value, "Ctrl+Shift+Page Down must extend to the document end");
    }
    {
        Fixture f;
        f.click(f.point(2.3f), 2u);
        expect(f.selected() == "first", "Double-clicking must select the word under the pointer");
        f.click(f.point(8.2f), 2u);
        expect(f.selected() == "second", "Double-click must find the clicked word rather than always selecting the first");
        f.click(f.point(8.2f), 3u);
        expect(f.selected() == "first second", "Triple-clicking must select the full value");
        unsigned visibleHighlights = 0u;
        for (const auto highlight : f.input().selectionEntities)
        {
            const auto* layer = f.scene.registry().try_get<RenderLayer2DComponent>(highlight);
            visibleHighlights += layer && layer->visible;
            expect(f.scene.registry().all_of<InputTransparent2DComponent>(highlight), "Selection paint must not intercept pointer hits");
        }
        expect(visibleHighlights == 1u, "A single-line selection must emit one visible highlight");
        ui_update_text_input_carets(f.scene, f.state, 20.0);
        expect(!f.scene.registry().get<RenderLayer2DComponent>(f.field.caret).visible, "A highlighted selection must hide the blinking caret");
        f.key(f.keys.left);
        expect(f.selected().empty() && f.input().caretByte == 0u, "An arrow must dismiss mouse selection without deleting text");
        f.click(f.point(3.0f));
        f.click(f.point(8.0f), 1u, true);
        expect(f.selected() == "st se", "Shift-click must extend from the existing mouse caret");
        f.click(f.point(2.3f), 0u);
        f.click(f.point(2.3f), 0u);
        expect(f.selected() == "first", "Timed consecutive clicks must derive a double-click count");
        f.click(f.point(2.3f), 0u);
        expect(f.selected() == "first second", "A third consecutive click must derive full selection");
    }
    {
        Fixture f;
        UiPointerButtonInput press;
        press.hasPoint = true; press.point = f.point(1.0f); press.button = PointerButton::eLeft;
        ui_handle_pointer_button(f.scene, f.atlas, f.state, press);
        ui_update_text_selection_drag(f.scene, f.atlas, f.state, true, true, f.point(8.0f));
        expect(f.selected() == "irst se", "Dragging must select continuously from the press caret");
        ui_update_text_selection_drag(f.scene, f.atlas, f.state, false, true, f.point(8.0f));
        expect(f.state.activeTextSelection == entt::null && f.state.pointerInputCapture == entt::null, "Releasing selection must finish capture");
        f.set("first second first second first second first second");
        expect(f.input().displayStartByte > 0u, "Long text must scroll to the active caret");
        f.key(f.keys.home);
        expect(f.input().displayStartByte == 0u && f.input().caretByte == 0u, "Moving Home must reveal the beginning of a long input");
        f.key(f.keys.selectAll, true);
        f.key(f.keys.copy, true);
        expect(f.clipboard == f.input().value, "Copy must include selected text outside the visible window");
        f.type('x');
        expect(f.input().displayStartByte == 0u &&
            f.scene.registry().get<TextComponent>(f.field.label).text == "x",
            "Replacing a scrolled selection must reveal the replacement immediately");
        f.set("first second first second first second first second");
        for (unsigned count = 0u; count < 30u; ++count) f.key(f.keys.backspace);
        expect(f.input().displayStartByte == 0u,
            "Deleting down to a short value must reclaim the horizontal viewport");
    }
    {
        Fixture f;
        f.key(f.keys.home);
        f.key(f.keys.deleteKey, true, true);
        expect(f.input().value == "second" && f.clipboard.empty(),
            "Ctrl+Shift+Delete must delete forwards by word without cutting to the clipboard");
        f.key(f.keys.right, false, true, false, false, UiInputAction::eRepeat);
        expect(f.selected() == "s", "Key repeat must extend keyboard selection");
        f.key(f.keys.selectAll, true, false, true);
        expect(f.selected() == "s", "AltGr must not trigger Ctrl+A");
        f.type(0x20ACu);
        expect(f.input().value == utf8(u8"€econd"), "AltGr character entry must replace the current selection");
        f.key(f.keys.backspace, false, false, true);
        expect(f.input().value == "second", "Alt+Backspace must undo on Windows");
        f.key(f.keys.backspace, false, true, true);
        expect(f.input().value == utf8(u8"€econd"), "Alt+Shift+Backspace must redo on Windows");
        f.set("first second third");
        f.click(f.point(2.0f), 2u);
        UiPointerButtonInput press;
        press.hasPoint = true; press.point = f.point(2.0f); press.button = PointerButton::eLeft;
        press.clickCount = 2u;
        ui_handle_pointer_button(f.scene, f.atlas, f.state, press);
        ui_update_text_selection_drag(f.scene, f.atlas, f.state, true, true, f.point(9.0f));
        expect(f.selected() == "first second", "Double-click dragging must select whole words");
        ui_update_text_selection_drag(f.scene, f.atlas, f.state, true, true, f.point(0.0f));
        expect(f.selected() == "first", "Reversing word dragging must retain the original word");
        ui_update_text_selection_drag(f.scene, f.atlas, f.state, false, true, f.point(0.0f));
        f.input().value = "external";
        ui_update_text_input_visual(f.scene, f.atlas, f.field.root);
        expect(f.input().caretByte == 8u && f.input().selectionAnchorByte == 8u &&
            f.input().undoHistory.empty(), "External value assignments must reset stale selection and history");
        f.input().value = "fresh value";
        ui_text_move_caret(f.input(), 2u, false);
        expect(f.input().caretByte == 2u && f.input().selectionAnchorByte == 2u,
            "The public caret operation must synchronise externally assigned text");
    }
    {
        Fixture f;
        const auto second = ui_create_search_field(f.ui, f.atlas, f.ui.root(), UiAlignment::eTopLeft,
            { 20.0f, 80.0f }, { 240.0f, 30.0f }, 5, 0u);
        f.scene.registry().get<TextInputComponent>(second.root).value = "another";
        f.layout();
        f.key(f.keys.tab);
        expect(f.state.focusedTextInput == second.root &&
            ui_text_selection(f.scene.registry().get<TextInputComponent>(second.root)).end == 7u, "Tab must focus the next input and select its value");
        f.key(f.keys.tab, false, true);
        expect(f.state.focusedTextInput == f.field.root, "Shift+Tab must focus the preceding field");
        f.key(f.keys.enter);
        expect(f.submissions == 1u, "Enter must submit a single-line input");
        f.key(f.keys.escape);
        expect(f.state.focusedTextInput == entt::null, "Escape must release text focus");
    }
    {
        Fixture f;
        f.keys.macOsBindings = true;
        f.key(f.keys.selectAll, false, false, false, true);
        expect(f.selected() == f.input().value, "Command+A must provide the native macOS select-all binding");
        f.key(f.keys.end);
        f.key(f.keys.left, false, false, true);
        expect(f.input().caretByte == 6u, "Option+Left must move by a word on macOS");
        f.key(f.keys.left, false, true, false, true);
        expect(f.selected() == "first ", "Command+Shift+Left must select to the line start");
    }
    std::cout << checks << " text editing, keyboard, clipboard, Unicode and pointer checks passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "text input: " << error.what() << '\n';
    return 1;
}
