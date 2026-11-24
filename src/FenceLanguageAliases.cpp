#include "FenceLanguageAliases.h"

#include <QHash>

namespace FenceLanguageAliases {

namespace {

const QHash<QString, QString>& table()
{
    static const QHash<QString, QString> entries = {
        { QStringLiteral("cpp"), QStringLiteral("file.cpp") },
        { QStringLiteral("c++"), QStringLiteral("file.cpp") },
        { QStringLiteral("c"), QStringLiteral("file.c") },
        { QStringLiteral("csharp"), QStringLiteral("file.cs") },
        { QStringLiteral("cs"), QStringLiteral("file.cs") },
        { QStringLiteral("java"), QStringLiteral("file.java") },
        { QStringLiteral("javascript"), QStringLiteral("file.js") },
        { QStringLiteral("js"), QStringLiteral("file.js") },
        { QStringLiteral("mjs"), QStringLiteral("file.mjs") },
        { QStringLiteral("cjs"), QStringLiteral("file.cjs") },
        { QStringLiteral("jsx"), QStringLiteral("file.jsx") },
        { QStringLiteral("typescript"), QStringLiteral("file.ts") },
        { QStringLiteral("ts"), QStringLiteral("file.ts") },
        { QStringLiteral("tsx"), QStringLiteral("file.tsx") },
        { QStringLiteral("python"), QStringLiteral("file.py") },
        { QStringLiteral("py"), QStringLiteral("file.py") },
        { QStringLiteral("go"), QStringLiteral("file.go") },
        { QStringLiteral("rust"), QStringLiteral("file.rs") },
        { QStringLiteral("rs"), QStringLiteral("file.rs") },
        { QStringLiteral("kotlin"), QStringLiteral("file.kt") },
        { QStringLiteral("kt"), QStringLiteral("file.kt") },
        { QStringLiteral("kts"), QStringLiteral("file.kt") },
        { QStringLiteral("swift"), QStringLiteral("file.swift") },
        { QStringLiteral("php"), QStringLiteral("file.php") },
        { QStringLiteral("ruby"), QStringLiteral("file.rb") },
        { QStringLiteral("rb"), QStringLiteral("file.rb") },
        { QStringLiteral("erb"), QStringLiteral("file.erb") },
        { QStringLiteral("perl"), QStringLiteral("file.pl") },
        { QStringLiteral("pl"), QStringLiteral("file.pl") },
        { QStringLiteral("pm"), QStringLiteral("file.pl") },
        { QStringLiteral("lua"), QStringLiteral("file.lua") },
        { QStringLiteral("scala"), QStringLiteral("file.scala") },
        { QStringLiteral("groovy"), QStringLiteral("file.groovy") },
        { QStringLiteral("dart"), QStringLiteral("file.dart") },
        { QStringLiteral("r"), QStringLiteral("file.r") },
        { QStringLiteral("haskell"), QStringLiteral("file.hs") },
        { QStringLiteral("hs"), QStringLiteral("file.hs") },
        { QStringLiteral("erlang"), QStringLiteral("file.erl") },
        { QStringLiteral("erl"), QStringLiteral("file.erl") },
        { QStringLiteral("elixir"), QStringLiteral("file.exs") },
        { QStringLiteral("exs"), QStringLiteral("file.exs") },
        { QStringLiteral("nim"), QStringLiteral("file.nim") },
        { QStringLiteral("clojure"), QStringLiteral("file.clj") },
        { QStringLiteral("clj"), QStringLiteral("file.clj") },
        { QStringLiteral("coffee"), QStringLiteral("file.coffee") },
        { QStringLiteral("elm"), QStringLiteral("file.elm") },
        { QStringLiteral("vhdl"), QStringLiteral("file.vhdl") },
        { QStringLiteral("ada"), QStringLiteral("file.ada") },
        { QStringLiteral("awk"), QStringLiteral("file.awk") },
        { QStringLiteral("asciidoc"), QStringLiteral("file.adoc") },
        { QStringLiteral("adoc"), QStringLiteral("file.adoc") },
        { QStringLiteral("haml"), QStringLiteral("file.haml") },
        { QStringLiteral("graphql"), QStringLiteral("file.graphql") },
        { QStringLiteral("glsl"), QStringLiteral("file.glsl") },
        { QStringLiteral("nix"), QStringLiteral("file.nix") },
        { QStringLiteral("scheme"), QStringLiteral("file.scm") },
        { QStringLiteral("scm"), QStringLiteral("file.scm") },
        { QStringLiteral("lisp"), QStringLiteral("file.lisp") },
        { QStringLiteral("tex"), QStringLiteral("file.tex") },
        { QStringLiteral("latex"), QStringLiteral("file.tex") },
        { QStringLiteral("diff"), QStringLiteral("file.diff") },
        { QStringLiteral("patch"), QStringLiteral("file.diff") },
        { QStringLiteral("tcl"), QStringLiteral("file.tcl") },
        { QStringLiteral("less"), QStringLiteral("file.less") },
        { QStringLiteral("svg"), QStringLiteral("file.svg") },
        { QStringLiteral("xsd"), QStringLiteral("file.xsd") },
        { QStringLiteral("hbs"), QStringLiteral("file.hbs") },
        { QStringLiteral("handlebars"), QStringLiteral("file.hbs") },
        { QStringLiteral("twig"), QStringLiteral("file.twig") },
        { QStringLiteral("qml"), QStringLiteral("file.qml") },
        { QStringLiteral("mel"), QStringLiteral("file.mel") },
        { QStringLiteral("smali"), QStringLiteral("file.smali") },
        { QStringLiteral("sv"), QStringLiteral("file.sv") },
        { QStringLiteral("stan"), QStringLiteral("file.stan") },
        { QStringLiteral("vala"), QStringLiteral("file.vala") },
        { QStringLiteral("pony"), QStringLiteral("file.pony") },
        { QStringLiteral("proto"), QStringLiteral("file.proto") },
        { QStringLiteral("scad"), QStringLiteral("file.scad") },
        { QStringLiteral("ldif"), QStringLiteral("file.ldif") },
        { QStringLiteral("properties"), QStringLiteral("file.properties") },
        { QStringLiteral("dts"), QStringLiteral("file.dts") },
        { QStringLiteral("bat"), QStringLiteral("file.bat") },
        { QStringLiteral("cmd"), QStringLiteral("file.bat") },
        { QStringLiteral("sql"), QStringLiteral("file.sql") },
        { QStringLiteral("bash"), QStringLiteral("file.sh") },
        { QStringLiteral("sh"), QStringLiteral("file.sh") },
        { QStringLiteral("shell"), QStringLiteral("file.sh") },
        { QStringLiteral("powershell"), QStringLiteral("file.ps1") },
        { QStringLiteral("ps1"), QStringLiteral("file.ps1") },
        { QStringLiteral("pwsh"), QStringLiteral("file.ps1") },
        { QStringLiteral("html"), QStringLiteral("file.html") },
        { QStringLiteral("css"), QStringLiteral("file.css") },
        { QStringLiteral("scss"), QStringLiteral("file.scss") },
        { QStringLiteral("json"), QStringLiteral("file.json") },
        { QStringLiteral("yaml"), QStringLiteral("file.yaml") },
        { QStringLiteral("yml"), QStringLiteral("file.yaml") },
        { QStringLiteral("toml"), QStringLiteral("file.toml") },
        { QStringLiteral("xml"), QStringLiteral("file.xml") },
        { QStringLiteral("ini"), QStringLiteral("file.ini") },
        { QStringLiteral("cmake"), QStringLiteral("CMakeLists.txt") },
        { QStringLiteral("dockerfile"), QStringLiteral("Dockerfile") },
        { QStringLiteral("docker"), QStringLiteral("Dockerfile") },
        { QStringLiteral("makefile"), QStringLiteral("Makefile") },
        { QStringLiteral("make"), QStringLiteral("Makefile") },
        { QStringLiteral("mk"), QStringLiteral("Makefile") },
    };
    return entries;
}

}

QString fileNameForTag(const QString& tag)
{
    if (tag.isEmpty())
        return {};
    return table().value(tag.toLower());
}

QStringList tags()
{
    return table().keys();
}

}
