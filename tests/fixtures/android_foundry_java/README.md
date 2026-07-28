# Foundry-Java Android export fixture

This fixture is the smallest annotated Java extension used by the Foundry
Android exporter integration tests. The tests compile `DemoExtension.java`
through the exact merged Foundry-Java annotation processor. The processor emits
the reflection-free module registry, callback trampoline, descriptor, and
narrow ProGuard rules, which the tests package into a deterministic module JAR
and consume through both explicit local files and a staged Maven repository.

The fixture deliberately contains no Android or Foundry host native library.
Foundry-Java owns descriptor and binding-payload validation; Foundry supplies
only the explicit artifact graph and requested ABI set.
