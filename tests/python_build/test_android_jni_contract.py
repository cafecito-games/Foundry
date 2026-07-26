from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import ModuleType

REPO_ROOT = Path(__file__).resolve().parents[2]
JNI_CONTRACT_PATH = REPO_ROOT / "platform/android/android_jni_contract.py"


def load_contract() -> ModuleType:
    if not JNI_CONTRACT_PATH.is_file():
        raise AssertionError(f"missing compiled Android JNI contract: {JNI_CONTRACT_PATH}")
    spec = importlib.util.spec_from_file_location("android_jni_contract_for_test", JNI_CONTRACT_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError(f"unable to load compiled Android JNI contract: {JNI_CONTRACT_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class AndroidJniContractTests(unittest.TestCase):
    contract: ModuleType

    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = load_contract()

    def compile_fixture(self, root: Path) -> Path:
        source = root / "src/games/cafecito/foundry/Fixture.java"
        source.parent.mkdir(parents=True)
        source.write_text(
            """
package games.cafecito.foundry;
public final class Fixture {
    public static native void ping();
    public native int under_score(String value);
    public static void notNative() {}
    public static final class Inner {
        public static native void nested();
    }
}
""".strip()
            + "\n",
            encoding="utf-8",
        )
        classes = root / "classes"
        classes.mkdir()
        subprocess.run(["javac", "-d", classes, source], check=True, capture_output=True)
        archive = root / "classes.jar"
        with zipfile.ZipFile(archive, "w") as output:
            for path in sorted(classes.rglob("*.class")):
                output.write(path, path.relative_to(classes).as_posix())
        return archive

    def test_derives_exact_jni_names_from_compiled_native_declarations(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = self.compile_fixture(Path(temporary))

            symbols = self.contract.derive_declared_jni_symbols(archive)

        self.assertEqual(
            (
                "Java_games_cafecito_foundry_Fixture_00024Inner_nested",
                "Java_games_cafecito_foundry_Fixture_ping",
                "Java_games_cafecito_foundry_Fixture_under_1score",
            ),
            symbols,
        )

    def test_rejects_empty_or_non_jar_compiled_declaration_input(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            empty = root / "empty.jar"
            with zipfile.ZipFile(empty, "w"):
                pass
            with self.assertRaisesRegex(self.contract.JniContractError, "no compiled classes"):
                self.contract.derive_declared_jni_symbols(empty)

            malformed = root / "malformed.jar"
            malformed.write_bytes(b"not a jar")
            with self.assertRaisesRegex(self.contract.JniContractError, "unable to read compiled classes"):
                self.contract.derive_declared_jni_symbols(malformed)


if __name__ == "__main__":
    unittest.main()
