"""Check scope hook generation without regenerating the complete port."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare  # noqa: E402
import font_layout  # noqa: E402
import font_numbers  # noqa: E402
import font_fit  # noqa: E402


class ScopeHooks(unittest.TestCase):
    def test_reflow_hooks(self):
        for rel in (
            font_layout.DRAW,
            font_layout.LINE,
            font_layout.ALIGN,
            "libs/nns/g2d/calls/Text_AlignAnchor.c",
            font_fit.DRAW,
        ):
            source = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
            output = font_layout.patch(source, rel)
            self.assertIn("int token = khdays_ui_text_layout_begin();", output)
            self.assertIn("khdays_ui_text_layout_actual(token);", output)
            self.assertIn("khdays_ui_text_layout_end(token);", output)
            if rel == font_layout.DRAW:
                self.assertLess(
                    output.index("if (khdays_ui_text_layout_glyph("),
                    output.index("khdays_ui_text_position(font, x, y);"),
                )

    def test_speaker_name_only(self):
        rel = "src/overlays/scenes/ov002_field/Ov002_SceneApplyPanelRequest.c"
        text = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
        for before, after in prepare.FILE_HOOKS[rel]:
            self.assertEqual(text.count(before), 1)
            text = text.replace(before, after)
        start = text.index('scope_push("field.dialogue.speaker")')
        end = text.index("khdays_ui_text_scope_pop();", start)
        name = text[start:end]
        self.assertIn("NNSi_G2dFontGetStringWidth(", name)
        self.assertEqual(name.count("Text_DrawDirectional_2("), 2)
        self.assertNotIn("Ov002_ReplaceOwnedString(", name)
        self.assertLess(end, text.index("nCount = Ov002_SceneLayoutPanelWindow("))
        dialogue = "src/overlays/scenes/ov002_field/Ov002_SceneDrawEntryLines.c"
        self.assertNotIn(dialogue, prepare.UI_HOOKS)
        self.assertNotIn(dialogue, prepare.FILE_HOOKS)

    def test_item_fit_hooks(self):
        source = prepare.transform((prepare.DECOMP / font_fit.ITEMS).read_text(encoding="utf-8"))
        output = font_layout.patch(source, font_fit.ITEMS)
        self.assertEqual(output.count("HD_FontFitShadow(pDst + nX, 0, nY + 3, 2, pText, 1, 0)"), 1)
        self.assertIn("Text_DrawWithShadow(pDst + nX, 0, nY + 3, 2, pText, 1)", output)

    def test_numeric_layout_hooks(self):
        for rel in (*font_numbers.COUNTERS, *font_numbers.TIMES, font_numbers.PAYLOAD):
            source = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
            output = font_layout.patch(source, rel)
            if rel in font_numbers.COUNTERS:
                self.assertIn("if (HD_FontCounter(", output)
                self.assertIn("nX -= DIGIT_STEP;", output)
                self.assertIn("nX - NNS_G2dFontGetCharWidthsFromIndex(pFont, nGlyph)->nWidth", output)
                self.assertNotIn("khdays_ui_text_metric(pFont, nGlyph, 2", output)
            elif rel in font_numbers.TIMES:
                self.assertIn("hdTime = HD_FontTime(", output)
                self.assertIn("if (hdTime &&", output)
                self.assertIn("DrawWithShadow(", output)
            else:
                self.assertIn("double positions[64]", output)
                self.assertIn("HD_FontDrawGlyph(owner, font,", output)
        rel = "src/overlays/scenes/ov008_camp_menu/Ov008_DrawStatusField.c"
        source = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
        self.assertEqual(font_layout.patch(source, rel), source)

    def test_parameter_comment(self):
        source = "void Example(int *menu, int *entry /* reloaded after every call, safely */, int *node) { return; }"
        output = prepare.ui_hook(source, "synthetic.c", "Example", "text", "mission_select.objectives", "scope")
        self.assertIn("Example__khdays_game(menu, entry, node);", output)
        wrapper = output[output.rfind("void Example(") :]
        self.assertLess(wrapper.index("scope_push("), wrapper.index("Example__khdays_game("))
        self.assertLess(wrapper.index("Example__khdays_game("), wrapper.index("scope_pop("))

    def test_mission_hooks(self):
        count = 0
        for rel, hooks in prepare.UI_HOOKS.items():
            if not any(hook[3] == "scope" and hook[2].startswith("mission_select.") for hook in hooks):
                continue
            text = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
            for hook in hooks:
                text = prepare.ui_hook(text, rel, *hook)
            if rel.endswith("MissionMenu_DrawInfo.c"):
                prefix = "Ov008" if "ov008" in rel else "Ov025"
                self.assertIn(f"{prefix}_MissionMenu_DrawInfo__khdays_game(pMenu, pEntry, pNode);", text)
            for before, after in prepare.FILE_HOOKS.get(rel, []):
                self.assertEqual(text.count(before), 1, (rel, before))
                text = text.replace(before, after)
            count += 1
        self.assertEqual(count, 12)

    def test_field_and_results_hooks(self):
        expected = {
            "Ov002_BeginTextCrawl": "field.hud.information",
            "Ov002_StepTextCrawl": "field.hud.information",
            "Ov002_HoldPanelScreen": "field.hud.information",
            "Ov002_SetCaptionText": "field.hud.information",
            "Ov002_BuildRunSummary": "field.hud.information",
            "Ov002_DrawLoadedCaption": "field.hud.area",
            "Ov002_DrawLoadedPortrait": "field.hud.status",
            "Ov002_DrawLoadedTally": "field.hud.status",
            "Ov002_DrawMessageCaption": "field.hud.status",
            "Ov002_ShowNoticeText": "field.hud.status",
            "Ov005_UpdateResultLabels": "mission.results.summary",
            "Ov005_RenderTextSurface": "mission.results.rewards",
        }
        found = set()
        for rel, hooks in prepare.UI_HOOKS.items():
            for name, part, scope, kind in hooks:
                if name not in expected:
                    continue
                self.assertEqual((part, scope, kind), ("text", expected[name], "scope"))
                source = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
                output = prepare.ui_hook(source, rel, name, part, scope, kind)
                wrapper = output[output.rfind(f"void {name}(") :]
                self.assertIn(f'scope_push("{scope}")', wrapper)
                self.assertLess(wrapper.index("scope_push("), wrapper.index(f"{name}__khdays_game("))
                self.assertLess(wrapper.index(f"{name}__khdays_game("), wrapper.index("scope_pop("))
                found.add(name)
        self.assertEqual(found, set(expected))

    def test_initial_instruction_and_world(self):
        count = 0
        for rel, hooks in prepare.FILE_HOOKS.items():
            if not rel.endswith("MainMenu_SetupTextSurfaces.c"):
                continue
            text = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
            for before, after in hooks:
                self.assertEqual(text.count(before), 1, (rel, before))
                text = text.replace(before, after)
            self.assertEqual(text.count('scope_push("mission_select.instruction")'), 1)
            self.assertEqual(text.count('scope_push("mission_select.world")'), 1)
            self.assertEqual(text.count("khdays_ui_text_scope_pop();"), 2)
            count += 1
        self.assertEqual(count, 2)

    def test_command_and_shop_hooks(self):
        expected = {
            "Ov002_DrawPanelRowText": "field.command",
            "Ov026_DrawStringShadowed": "shop",
            "Ov026_DrawNumber3Shadowed": "shop",
            "Ov026_DrawDescriptionText": "shop",
            "Ov026_OpenShopDetailPanel": "shop",
        }
        found = set()
        for rel, hooks in prepare.UI_HOOKS.items():
            for name, part, scope, kind in hooks:
                if name not in expected:
                    continue
                self.assertEqual((part, scope, kind), ("text", expected[name], "scope"))
                text = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
                output = prepare.ui_hook(text, rel, name, part, scope, kind)
                wrapper = output[output.rfind(f"void {name}(") :]
                self.assertLess(wrapper.index("scope_push("), wrapper.index(f"{name}__khdays_game("))
                self.assertLess(wrapper.index(f"{name}__khdays_game("), wrapper.index("scope_pop("))
                found.add(name)
        self.assertEqual(found, set(expected))
        self.assertNotIn(
            "src/overlays/scenes/ov002_field/Ov002_BuildPanelLabels.c", prepare.UI_HOOKS
        )  # Enemy-name captions are not command labels.

    def test_notification_layout_observer(self):
        rel = "src/overlays/scenes/ov002_field/Ov002_BlitTileRegion.c"
        text = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
        for before, after in prepare.FILE_HOOKS[rel]:
            self.assertEqual(text.count(before), 1)
            text = text.replace(before, after)
        self.assertIn("khdays_ui_notification_layout(pReq, nFlag)", text)
        self.assertIn("Ov002_WriteMapRow(pReq->nDest", text)
        rel = "src/overlays/scenes/ov002_field/Ov002_BuildTextItem.c"
        self.assertIn(("Ov002_BuildTextItem", "text", "field.breakthrough", "scope"), prepare.UI_HOOKS[rel])

    def test_obtained_private_surface(self):
        rel = "src/overlays/scenes/ov002_field/Ov002_PanelDrawSlotCursor.c"
        text = prepare.transform((prepare.DECOMP / rel).read_text(encoding="utf-8"))
        for before, after in prepare.FILE_HOOKS[rel]:
            self.assertEqual(text.count(before), 1)
            text = text.replace(before, after)
        self.assertIn("khdays_ui_text_obtained_wants(nSlot, nShift == -2)", text)
        self.assertIn("0x4b8 + nSlot * 12", text)
        self.assertLess(text.index("TileSurface_Init4bpp((int)surface"), text.index("HD_FontFitShadow(surface"))
        self.assertIn("HD_FontFitShadow(surface, 0, 3, 2, text, 1, 1)", text)
        self.assertLess(text.index("obtained_capture(surface)"), text.index("FreeAllListNodeSubBuffers(surface)"))
        self.assertNotIn("GX_Load", text)  # Private captures never overwrite the command cache's VRAM.


if __name__ == "__main__":
    unittest.main()
