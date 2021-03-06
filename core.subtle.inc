// auto generated by gen.py, do not modify!
#ifndef SUBTLE_CORE_INC_H
#define SUBTLE_CORE_INC_H
const char* CORE_SOURCE =
"Fn.new{\n"
"Object.!= = Fn.new{|o| return ! (this == o); };\n"
"Object.println = Fn.new{\n"
"    this.print;\n"
"    \"\n\".print;\n"
"};\n"
"Object.init = Fn.new{};\n"
"\n"
"let MethodIterator = {\n"
"    init: Fn.new{|obj, slot|\n"
"        this._obj = obj;\n"
"        this._slot = slot;\n"
"        return this;\n"
"    },\n"
"    iterMore: Fn.new{|v| return this._obj.rawIterMore(v); },\n"
"    iterNext: Fn.new{|v|\n"
"        return this._obj\n"
"            .rawGetSlot(this._slot)\n"
"            .callWithThis(this._obj, v);\n"
"    }\n"
"};\n"
"\n"
"let fromSlot = Fn.new{|slot|\n"
"    return Fn.new{ return MethodIterator.clone.init(this, slot); };\n"
"};\n"
"\n"
"Object.keys   = fromSlot.call(\"rawIterSlotsNext\");\n"
"Object.values = fromSlot.call(\"rawIterValueNext\");\n"
"Map.keys      = fromSlot.call(\"rawIterKeyNext\");\n"
"Map.values    = fromSlot.call(\"rawIterValueNext\");\n"
"\n"
"List.fromIterator = Fn.new{|it|\n"
"    let list = List.new();\n"
"    for (item in it)\n"
"        list.add(item);\n"
"    return list;\n"
"};\n"
"\n"
"List.== = Fn.new{|o|\n"
"    if (Object.same(this, o)) return true;\n"
"    if (Object.rawType(o) != \"list\" || this.length != o.length)\n"
"        return false;\n"
"    for (i in 0...this.length)\n"
"        if (this.get(i) != o.get(i))\n"
"            return false;\n"
"    return true;\n"
"};\n"
"\n"
"Map.== = Fn.new{|o|\n"
"    if (Object.same(this, o)) return true;\n"
"    if (Object.rawType(o) != \"map\" || this.length != o.length)\n"
"        return false;\n"
"    let SENTINEL = {};\n"
"    for (k in this.keys)\n"
"        if (this.get(k) != o.get(k, SENTINEL))\n"
"            return false;\n"
"    return true;\n"
"};\n"
"}.call;\n"
;
#endif
