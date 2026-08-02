from __future__ import annotations

import unittest

from srw64_w0.inventory import analyze_entry, group_sequences, summarize_inventory
from srw64_w0.text_ir import TextEntry
from srw64_w0.translation import TranslationRow


def sample_entry(text_id: int, units: tuple[int, ...]) -> TextEntry:
    return TextEntry(
        table_id=0,
        text_id=text_id,
        table_base=0x200,
        descriptor_offset=0x204 + text_id * 8,
        relative_offset=0x100 + text_id * 0x20,
        data_offset=0x300 + text_id * 0x20,
        byte_size=8 + len(units) * 2,
        header=bytes(8),
        units=units,
    )


class TextInventoryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.mapping = {1: "日", 2: "能力"}
        self.policy = {0x0124: "name_placeholder"}

    def test_analysis_counts_ligatures_controls_and_preserved_tokens(self) -> None:
        analysis = analyze_entry(
            sample_entry(
                1,
                (1, 0, 2, 0xFFFE, 0x0124, 1, 0xFFFD, 0xFFFF),
            ),
            self.mapping,
            self.policy,
        )

        self.assertEqual(analysis.source, "日 能力<BR><G:0124>日<STOP><END>")
        self.assertEqual(analysis.visible_glyph_cells, 5)
        self.assertEqual(analysis.mapped_glyph_cells, 3)
        self.assertEqual(analysis.decoded_characters, 5)
        self.assertEqual(analysis.decoded_nonspace_characters, 4)
        self.assertEqual(analysis.preserved_token_cells, 1)
        self.assertEqual(analysis.preserved_token_ids, (0x0124,))
        self.assertEqual(analysis.text_class, "text")
        self.assertEqual(analysis.flow_class, "stop")

    def test_unresolved_glyph_blocks_translation(self) -> None:
        analysis = analyze_entry(
            sample_entry(2, (0x0125, 0xFFFF)), self.mapping, self.policy
        )

        self.assertEqual(analysis.text_class, "unresolved")
        self.assertEqual(analysis.translation_status, "blocked_unresolved")
        self.assertEqual(analysis.unresolved_glyph_ids, (0x0125,))

    def test_whitespace_only_entry_is_not_a_translation_candidate(self) -> None:
        analysis = analyze_entry(
            sample_entry(3, (0, 0xFFFF)), self.mapping, self.policy
        )

        self.assertEqual(analysis.text_class, "whitespace_only")
        self.assertEqual(analysis.translation_status, "nontext")

    def test_exact_sequence_grouping_preserves_context_keys(self) -> None:
        shared = (1, 0xFFFE, 2, 0xFFFF)
        analyses = [
            analyze_entry(sample_entry(1, shared), self.mapping, self.policy),
            analyze_entry(sample_entry(2, shared), self.mapping, self.policy),
            analyze_entry(
                sample_entry(3, (1, 2, 0xFFFF)), self.mapping, self.policy
            ),
        ]
        groups = group_sequences(analyses)

        self.assertEqual(len(groups), 2)
        self.assertEqual(groups[shared].keys, ["t00_00001", "t00_00002"])

    def test_summary_separates_candidates_tokens_and_duplicates(self) -> None:
        shared = (1, 0xFFFF)
        analyses = [
            analyze_entry(sample_entry(1, shared), self.mapping, self.policy),
            analyze_entry(sample_entry(2, shared), self.mapping, self.policy),
            analyze_entry(
                sample_entry(3, (0x0124, 0xFFFF)), self.mapping, self.policy
            ),
        ]
        groups = group_sequences(analyses)
        overlay = {
            "t00_00001": TranslationRow(
                key="t00_00001",
                table_id=0,
                text_id=1,
                target="中<END>",
                note="test",
            )
        }

        summary = summarize_inventory(analyses, groups, overlay)
        self.assertEqual(summary["totals"]["entries"], 3)
        self.assertEqual(summary["totals"]["candidate_entries"], 2)
        self.assertEqual(summary["totals"]["unique_sequences"], 2)
        self.assertEqual(summary["totals"]["duplicate_entry_instances"], 1)
        self.assertEqual(summary["text_classes"], {"text": 2, "token_only": 1})
        self.assertEqual(summary["overlay"]["translated_entries"], 1)
        self.assertEqual(
            summary["catalog_statuses"],
            {"partially_translated": 1, "nontext": 1},
        )
        self.assertEqual(summary["duplicate_groups"]["size_buckets"], {"2": 1, "1": 1})
        self.assertEqual(summary["duplicate_groups"]["maximum_group_size"], 2)


if __name__ == "__main__":
    unittest.main()
