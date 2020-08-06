commit 39ea228d3f2538bdfa74137d339e1a9e7d05240e 06. Aug. 2020:

+ modified:   std/bits/libcxx/tuple: commented out lines 1332-1343 due to piecewise_construct_t having no variable name  
+ modified:   std/initializer_list: some error with the namespace, changed to "using namespace std;"  
+ modified:   std/iostream: type String has no member called 'max_size()' (only basic_const_char_array has a member function called 'max_size()'), changed to 'buf_size()'  **bandaid since variable names are confusing and opaque due to the usage of 'auto', behaviour might be unpredictable**, see lines 69 & 80  
+ modified:   std/types/string.h: class basic_const_char_array can not have a dynamic allocated member 'CharT bs[]', changed to pointer 'CharT* bs', see line 1142  

