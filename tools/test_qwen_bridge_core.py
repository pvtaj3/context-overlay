import unittest
from qwen_bridge_core import (build_chat_request, build_context, build_search_url,
                               retry_delays, sanitize_query)

class BridgeCoreTests(unittest.TestCase):
    def test_sanitizes_and_bounds_query(self):
        self.assertEqual(sanitize_query("  hello\n world "), "hello world")
        self.assertEqual(len(sanitize_query("x" * 900)), 512)
        with self.assertRaises(ValueError): sanitize_query(" \n ")

    def test_url_and_prompt_are_bounded(self):
        url = build_search_url("http://127.0.0.1:8787/search", "a & b", 99)
        self.assertIn("a%20%26%20b", url)
        request = build_chat_request("question", [{"title": "T", "url": "U", "snippet": "S"}])
        self.assertEqual(request["messages"][1]["content"], "question")
        self.assertIn("[1] T", request["messages"][0]["content"])

    def test_retry_backoff_is_bounded(self):
        self.assertEqual(retry_delays(3), [0.25, 0.5, 1.0])
        self.assertLessEqual(len(retry_delays(99)), 5)

if __name__ == "__main__": unittest.main()
