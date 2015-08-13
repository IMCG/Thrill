%module(directors="1") c7a
%{

#include "c7a_python.hpp"

using namespace c7a;

%}

%feature("director") GeneratorFunction;

%feature("director") MapFunction;
%feature("director") FilterFunction;

%feature("director") KeyExtractorFunction;
%feature("director") ReduceFunction;

%define CallbackHelper(FunctionType,function_var) %{
   if not isinstance(function_var, FunctionType) and callable(function_var):
      class CallableWrapper(FunctionType):
         def __init__(self, f):
            super(CallableWrapper, self).__init__()
            self.f_ = f
         def __call__(self, index):
            return self.f_(index)
      function_var = CallableWrapper(function_var)
%}
%enddef

%define CallbackHelper2(FunctionType1,function_var1,FunctionType2,function_var2) %{
   if not isinstance(function_var1, FunctionType1) and callable(function_var1):
      class CallableWrapper(FunctionType1):
         def __init__(self, f):
            super(CallableWrapper, self).__init__()
            self.f_ = f
         def __call__(self, index):
            return self.f_(index)
      function_var1 = CallableWrapper(function_var1)
   if not isinstance(function_var2, FunctionType2) and callable(function_var2):
      class CallableWrapper(FunctionType2):
         def __init__(self, f):
            super(CallableWrapper, self).__init__()
            self.f_ = f
         def __call__(self, index):
            return self.f_(index)
      function_var2 = CallableWrapper(function_var2)
%}
%enddef

%feature("pythonprepend") c7a::PyContext::Generate(GeneratorFunction&, size_t)
   CallbackHelper(GeneratorFunction, generator_function)

%feature("pythonprepend") c7a::PyDIA::Map(MapFunction&) const
   CallbackHelper(MapFunction, map_function)

%feature("pythonprepend") c7a::PyDIA::Filter(FilterFunction&) const
   CallbackHelper(FilterFunction, filter_function)

%feature("pythonprepend") c7a::PyDIA::ReduceBy(KeyExtractorFunction&, ReduceFunction&) const
CallbackHelper2(KeyExtractorFunction, key_extractor, ReduceFunction, reduce_function)

%include <std_string.i>
%include <std_vector.i>
%include <std_shared_ptr.i>

%shared_ptr(c7a::PyContext)
%shared_ptr(c7a::api::Context)

%template(VectorPyObject) std::vector<PyObject*>;
%template(VectorString) std::vector<std::string>;

%template(VectorPyContext) std::vector<std::shared_ptr<c7a::PyContext>>;

%ignore c7a::api::HostContext::ConstructLocalMock;

%feature("pythonappend") c7a::PyContext::PyContext(HostContext&, size_t) %{
    # acquire a reference to the HostContext
    self._host_context = host_context
    %}

%include <c7a/api/context.hpp>
%include "c7a_python.hpp"
