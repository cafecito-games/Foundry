# A fully qualified chain names a generic tagged union just as directly as a bare identifier, so it
# has no bare form outside the union's own declaration either.
namespace generic_union_demo.consumer
import generic_union_demo.library

func leak() -> Variant:
	return generic_union_demo.library.NamespacedHolder.Value(123)
