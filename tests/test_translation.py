from __future__ import annotations

import unittest

from srw64_w0.text_ir import TextEntry
from srw64_w0.translation import (
    TranslationError,
    allocate_target_characters,
    decode_source,
    encode_target,
    parse_mixed_text,
    reverse_glyph_map,
    validate_target,
)


def sample_entry() -> TextEntry:
    return TextEntry(
        table_id=0,
        text_id=1,
        table_base=0x200,
        descriptor_offset=0x204,
        relative_offset=0x20,
        data_offset=0x220,
        byte_size=18,
        header=bytes(8),
        units=(1, 0xFFFE, 0x0124, 0xFFFD, 0xFFFF),
    )


class TranslationTokenTests(unittest.TestCase):
    def test_source_view_keeps_controls_and_unknown_glyphs(self) -> None:
        self.assertEqual(
            decode_source(sample_entry(), {1: "日"}),
            "日<BR><G:0124><STOP><END>",
        )

    def test_target_with_exact_token_sequence_validates(self) -> None:
        tokens = validate_target(
            sample_entry(), "中<BR><G:0124><STOP><END>", {1: "日"}
        )
        self.assertEqual(
            encode_target(tokens, reverse_glyph_map({1: "日"}), {"中": 0x081F}),
            (0x081F, 0xFFFE, 0x0124, 0xFFFD, 0xFFFF),
        )

    def test_control_reordering_is_rejected(self) -> None:
        with self.assertRaisesRegex(TranslationError, "required token sequence changed"):
            validate_target(
                sample_entry(), "中<STOP><G:0124><BR><END>", {1: "日"}
            )

    def test_invalid_or_nonfinal_end_is_rejected(self) -> None:
        with self.assertRaisesRegex(TranslationError, "one final"):
            validate_target(sample_entry(), "中<END><BR><G:0124><STOP>", {1: "日"})

    def test_han_characters_are_redrawn_even_if_already_mapped(self) -> None:
        tokens = {"t00_00001": parse_mixed_text("中A中<END>")}
        allocations = allocate_target_characters(
            tokens, {"中": 0x0500, "A": 11}, 0x081F, 0x0AA6
        )
        self.assertEqual(allocations, {"中": 0x081F})


if __name__ == "__main__":
    unittest.main()
