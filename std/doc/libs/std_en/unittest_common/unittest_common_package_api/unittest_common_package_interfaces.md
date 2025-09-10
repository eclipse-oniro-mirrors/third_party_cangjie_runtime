# Interfaces

## interface DataProvider

```cangjie
public interface DataProvider<T> {
    func provide(): Iterable<T>
}
```

Function: A component of [DataStrategy](#interface-datastrategy) used to provide test data, where T specifies the data type provided by the provider.

### func provide()

```cangjie
func provide(): Iterable<T>
```

Function: Retrieves a data iterator.

Return Value:

- [Iterable](../../core/core_package_api/core_package_interfaces.md#interface-iterablee)\<T> - Data iterator.

### extend\<T> Array\<T> <: DataProvider\<T>

```cangjie
extend<T> Array<T> <: DataProvider<T>
```

Function: Extends [Array](../../core/core_package_api/core_package_structs.md#struct-arrayt)<T>.

#### func provide()

```cangjie
public func provide(): Iterable<T>
```

Function: Retrieves a data iterator.

Return Value:

- [Iterable](../../core/core_package_api/core_package_interfaces.md#interface-iterablee)\<T> - Data iterator.

### extend\<T> Range\<T> <: DataProvider\<T>

```cangjie
extend<T> Range<T> <: DataProvider<T>
```

Function: Extends [Range](../../core/core_package_api/core_package_structs.md#struct-ranget-where-t--countablet--comparablet--equatablet)<T>.

#### func provide()

```cangjie
public func provide(): Iterable<T>
```

Function: Retrieves a data iterator.

Return Value:

- [Iterable](../../core/core_package_api/core_package_interfaces.md#interface-iterablee)\<T> - Data iterator.

## interface DataShrinker\<T>

```cangjie
public interface DataShrinker<T> {
    func shrink(value: T): Iterable<T>
}
```

Function: A component of [DataStrategy](#interface-datastrategy) used to shrink data during testing, where T specifies the data type processed by this shrinker.

### func shrink(T)

```cangjie
func shrink(value: T): Iterable<T>
```

Function: Takes a value of type T and generates a collection of smaller values. What constitutes "smaller" depends on the data type.

Parameters:

- value: T - The value to be shrunk.

Return Value:

- [Iterable](../../core/core_package_api/core_package_interfaces.md#interface-iterablee)\<T> - Collection of smaller values, returns an empty collection when data can no longer be shrunk.

## interface DataStrategy

```cangjie
public interface DataStrategy<T> {
    func provider(configuration: Configuration): DataProvider<T>
    func shrinker(configuration: Configuration): DataShrinker<T>
    prop isInfinite: Bool
}
```

Function: Strategy for providing data for parameterized tests, where T specifies the data type operated by this strategy.

### prop isInfinite

```cangjie
prop isInfinite: Bool
```

Function: Whether the data is inexhaustible.

Type: [Bool](../../core/core_package_api/core_package_intrinsics.md#bool)

### func provider(Configuration)

```cangjie
func provider(configuration: Configuration): DataProvider<T>
```

Function: Retrieves the component for providing test data.

Parameters:

- configuration: [Configuration](unittest_common_package_classes.md#class-configuration) - Configuration information.

Return Value:

- [DataProvider](../../unittest_common/unittest_common_package_api/unittest_common_package_interfaces.md#interface-dataprovider)\<T> - Component object for providing test data.

### func shrinker(Configuration)

```cangjie
func shrinker(configuration: Configuration): DataShrinker<T>
```

Function: Retrieves the component for shrinking test data.

Parameters:

- configuration: [Configuration](unittest_common_package_classes.md#class-configuration) - Configuration information.

Return Value:

- [DataShrinker](#interface-datashrinkert)\<T> - Component object for shrinking test data.

### extend\<T> Array\<T> <: DataStrategy\<T>

```cangjie
extend<T> Array<T> <: DataStrategy<T>
```

Function: Extends [Array](../../core/core_package_api/core_package_structs.md#struct-arrayt)<T>.

#### prop isInfinite

```cangjie
public prop isInfinite: Bool
```

Function: Whether the data is inexhaustible.

Type: [Bool](../../core/core_package_api/core_package_intrinsics.md#bool)

#### func provider(Configuration)

```cangjie
public func provider(configuration: Configuration): DataProvider<T>
```

Function: Retrieves the component for providing test data.

Parameters:

- configuration: [Configuration](unittest_common_package_classes.md#class-configuration) - Configuration information.

Return Value:

- [DataProvider](../../unittest_common/unittest_common_package_api/unittest_common_package_interfaces.md#interface-dataprovider)\<T> - Component object for providing test data.

#### func shrinker(Configuration)

```cangjie
func shrinker(configuration: Configuration): DataShrinker<T>
```

Function: Retrieves the component for shrinking test data.

Parameters:

- configuration: [Configuration](unittest_common_package_classes.md#class-configuration) - Configuration information.

Return Value:

- [DataShrinker](#interface-datashrinkert)\<T> - Component object for shrinking test data.

### extend\<T> Range\<T> <: DataStrategy\<T>

```cangjie
extend<T> Range<T> <: DataStrategy<T>
```

Function: Extends [Range](../../core/core_package_api/core_package_structs.md#struct-ranget-where-t--countablet--comparablet--equatablet)<T>.

#### prop isInfinite

```cangjie
public prop isInfinite: Bool
```

Function: Whether the data is inexhaustible.

Type: [Bool](../../core/core_package_api/core_package_intrinsics.md#bool)

#### func provider(Configuration)

```cangjie
public func provider(configuration: Configuration): DataProvider<T>
```

Function: Retrieves the component for providing test data.

Parameters:

- configuration: [Configuration](unittest_common_package_classes.md#class-configuration) - Configuration information.

Return Value:

- [DataProvider](../../unittest_common/unittest_common_package_api/unittest_common_package_interfaces.md#interface-dataprovider)\<T> - Component object for providing test data.

#### func shrinker(Configuration)

```cangjie
func shrinker(configuration: Configuration): DataShrinker<T>
```

Function: Retrieves the component for shrinking test data.

Parameters:

- configuration: [Configuration](unittest_common_package_classes.md#class-configuration) - Configuration information.

Return Value:

- [DataShrinker](#interface-datashrinkert)\<T> - Component object for shrinking test data.

## interface PrettyPrintable

```cangjie
public interface PrettyPrintable {
    func pprint(to: PrettyPrinter): PrettyPrinter
}
```

Function: Types implementing this interface can be printed with proper color and indentation formatting.

### func pprint(PrettyPrinter)

```cangjie
func pprint(to: PrettyPrinter): PrettyPrinter
```

Function: Prints the type value to the specified printer.

Parameters:

- to: [PrettyPrinter](./unittest_common_package_classes.md#class-prettyprinter) - Printer.

Return Value:

- [PrettyPrinter](./unittest_common_package_classes.md#class-prettyprinter) - Printer.

### extend\<T> Array\<T> <: PrettyPrintable where T <: PrettyPrintable

```cangjie
extend<T> Array<T> <: PrettyPrintable where T <: PrettyPrintable
```

Function: Extends [Array](../../core/core_package_api/core_package_structs.md#struct-arrayt)\<T> to implement [PrettyPrintable](#interface-prettyprintable).

#### func pprint(PrettyPrinter)

```cangjie
public func pprint(to: PrettyPrinter): PrettyPrinter
```

Function: Prints the type value to the specified printer.

Parameters:

- to: [PrettyPrinter](./unittest_common_package_classes.md#class-prettyprinter) - Printer.

Return Value:

- [PrettyPrinter](./unittest_common_package_classes.md#class-prettyprinter) - Printer.

### extend\<T> ArrayList\<T> <: PrettyPrintable where T <: PrettyPrintable

```cangjie
extend<T> ArrayList<T>  <: PrettyPrintable where T <: PrettyPrintable
```

Function: Extends [ArrayList](../../collection/collection_package_api/collection_package_class.md#class-arraylistt)<T> to implement [PrettyPrintable](#interface-prettyprintable).

#### func pprint(PrettyPrinter)

```cangjie
public func pprint(to: PrettyPrinter): PrettyPrinter
```

Function: Prints the type value to the specified printer.

Parameters:

- to: [PrettyPrinter](./unittest_common_package_classes.md#class-prettyprinter) - Printer.

Return Value:

- [PrettyPrinter](./unittest_common_package_classes.md#class-prettyprinter) - Printer.

## interface KeyFor

```cangjie
public interface KeyFor<T> {
    prop name: String
}
```

Function: The type of configuration keys in [Configuration](../../unittest_common/unittest_common_package_api/unittest_common_package_classes.md#class-configuration).

Custom configuration key-values can be defined via [@UnitestOption](./../../unittest_testmacro/unittest_testmacro_package_api/unittest_testmacro_package_macros.md#unittestoption-macro). Built-in unittest configuration items can be obtained according to the [naming rules](../../unittest_testmacro/unittest_testmacro_package_api/unittest_testmacro_package_macros.md#customassertion-macro). For example, the `randomSeed` can be extracted from [Configuration](../../unittest_common/unittest_common_package_api/unittest_common_package_classes.md#class-configuration) using the `KeyRandomSeed.randomSeed` key.

### prop name

```cangjie
prop name: String
```

Function: String representation of the key name used in [Configuration](./unittest_common_package_classes.md#class-configuration).

Type: [String](../../core/core_package_api/core_package_structs.md#struct-string).