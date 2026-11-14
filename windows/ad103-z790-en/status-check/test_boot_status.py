import struct
import unittest
from boot_status import decode


class RecordTests(unittest.TestCase):
    def setUp(self):
        self.record = [0x31535547, 1, 32, 0x270510de, 0x513e1462, 4, 7, 0]

    def test_verified_handoff_never_claims_live_state(self):
        result = decode(struct.pack('<8I', *self.record), 6)
        self.assertEqual(result['handoff_state'], 'UNLOCK_VERIFIED_AT_EFI_HANDOFF')
        self.assertEqual(result['live_gpu_state'], 'UNKNOWN')

    def test_other_states_are_not_unlocked_or_locked(self):
        for state in (1, 2, 3):
            record = self.record.copy()
            record[5:7] = [state, 0]
            result = decode(struct.pack('<8I', *record), 6)
            self.assertNotEqual(result['handoff_state'], 'UNLOCK_VERIFIED_AT_EFI_HANDOFF')
            self.assertEqual(result['live_gpu_state'], 'UNKNOWN')

    def test_wrong_fingerprint_or_checks_rejected(self):
        for index, value in [(0, 0), (1, 2), (2, 40), (3, 0), (4, 0), (5, 9), (6, 0), (7, 1)]:
            with self.subTest(index=index):
                record = self.record.copy()
                record[index] = value
                with self.assertRaises(ValueError):
                    decode(struct.pack('<8I', *record), 6)

    def test_persistent_or_wrong_attributes_rejected(self):
        for attrs in (0, 2, 7):
            with self.assertRaises(ValueError):
                decode(struct.pack('<8I', *self.record), attrs)
        with self.assertRaises(ValueError):
            decode(b'', 6)


if __name__ == '__main__':
    unittest.main()
