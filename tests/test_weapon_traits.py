import unittest

from srw64_native.weapon_traits import weapon_traits


class WeaponTraitsTests(unittest.TestCase):
    def test_melee_and_post_move_are_separate_from_name(self):
        traits = weapon_traits("アイアンネット", "格アイアンネットP")
        self.assertEqual(traits["display_name"], "アイアンネット")
        self.assertEqual(traits["markers"], [{"token": "格", "label": "格斗武器"},
                                             {"token": "P", "label": "可移动后使用"}])

    def test_map_name_variant_and_letters_inside_name(self):
        traits = weapon_traits("ハイパーメガ粒子砲MAP", "射ハイパーメガ粒子砲BMAP")
        self.assertEqual(traits["display_name"], "ハイパーメガ粒子砲")
        self.assertEqual([m["token"] for m in traits["markers"]], ["射", "B", "MAP"])
        traits = weapon_traits("TYPEP", "射TYPEPB")
        self.assertEqual(traits["display_name"], "TYPEP")
        self.assertEqual([m["token"] for m in traits["markers"]], ["射", "B"])

    def test_unmatched_text_is_preserved_without_guessing_flags(self):
        traits = weapon_traits("原名称P", "格別の名前P")
        self.assertFalse(traits["parsed"])
        self.assertEqual(traits["display_name"], "原名称P")
        self.assertEqual(traits["markers"], [])


if __name__ == "__main__":
    unittest.main()
