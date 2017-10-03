+++
title = "unchecked<T, EC>"
weight = 10
+++

## Outcome 2.0 namespace

It is recommended that you refer to entities from this Outcome 2.0 via the following namespace alias:

{{% snippet "using_result.cpp" "namespace" %}}

As patches and modifications are applied to this library, namespaces get permuted in order
not to break any backward compatibility. At some point namespace `outcome::v2` will be defined,
and this will be the prefered namespace. Until then `OUTCOME_V2_NAMESPACE` denotes the most recently
updated version, getting closer to `outcome::v2`.

## Creating `unchecked<>`

We will define a function that converts an `std::string` to an `int`. This function can fail for a number of reasons;
if it does we want to communicate the failure reason.

{{% snippet "using_result.cpp" "convert_decl" %}}

Class template `unchecked<T, EC>` has two template parameters. The first (`T`) represents the type of the object
returned from the function upon success; the second (`EC`) is the type of object containing information about the reason
for failure when the function fails. A `unchecked<T, EC>` object either stores a `T` or an `EC` at any given moment,
and is therefore conceptually similar to `variant<T, EC>`. `EC` is defaulted to `std::error_code`.
If both `T` and `EC` are trivially copyable, `unchecked<T, EC>` is also trivially copyable.

{{% notice note %}}
Both `unchecked<T, EC>` and `checked<T, EC>` are simplified typedefs for `result<T, EC>`, but are instances
of `result<T, EC>`. Anything we say about `unchecked<>` can also be said about `checked<T, EC>` and
`result<T, EC>`, unless otherwise specified.
{{% /notice %}}

Now, we will define an enumeration describing different failure situations during conversion.

{{% snippet "using_result.cpp" "enum" %}}

Assume we have plugged it into `std::error_code` framework, as described in [this section](error_code).

One notable effect of such plugging is that `ConversionErrc` is now convertible to `std::error_code`.
Now we can implement function `convert` as follows: 

{{% snippet "using_result.cpp" "convert" %}}

`unchecked<T, EC>` is convertible from any `T2` convertible to `T` as well as any `EC2` convertible to `EC`,
provided that the conversion is not ambiguous. If some type `X` is both convertible to `T` and `EC`, 
conversion to `unchecked<T, EC>` fails to compile. In this case you need to use one of the tagged constructors:

{{% snippet "using_result.cpp" "explicit" %}}