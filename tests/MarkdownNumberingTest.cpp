#include "TextTransforms.h"

#include <QCoreApplication>
#include <QDebug>

namespace {
bool check(const QString& name, const QString& actual, const QString& expected)
{
    if (actual == expected)
        return true;
    qCritical().noquote() << name << "\nExpected:\n" << expected << "\nActual:\n" << actual;
    return false;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    const QString sections = QStringLiteral(
        "# 1. Chapter One\n## 12 Introduction\n### 9.9 Previous\n#### Details\n##### More\n###### Deep\n"
        "# 2 Chapter Two\n#### Skipped\n## 7. Again\n");
    const QString expectedSections = QStringLiteral(
        "# Chapter One\n## 1. Introduction\n### 1.1 Previous\n#### 1.1.1 Details\n##### 1.1.1.1 More\n###### 1.1.1.1.1 Deep\n"
        "# Chapter Two\n#### 1.1.1 Skipped\n## 2. Again\n");
    const QString renumberedSections = TextTransforms::updateSectionNumbers(sections);
    ok &= check(QStringLiteral("section hierarchy, H1 cleanup, reset, and skipped levels"),
                renumberedSections, expectedSections);
    ok &= check(QStringLiteral("section numbering idempotency"),
                TextTransforms::updateSectionNumbers(renumberedSections), expectedSections);
    ok &= check(QStringLiteral("section fenced code is untouched"),
                TextTransforms::updateSectionNumbers(QStringLiteral("## 4. Before\n~~~\n## 99. Code\n~~~\n## 8 After\n")),
                QStringLiteral("## 1. Before\n~~~\n## 99. Code\n~~~\n## 2. After\n"));
    ok &= check(QStringLiteral("section selection changes only selected lines"),
                TextTransforms::updateSectionNumbers(QStringLiteral("## 8. One\n## 9. Two\n## 4. Three\n"), 1, 1),
                QStringLiteral("## 8. One\n## 2. Two\n## 4. Three\n"));

    const QString lists = QStringLiteral(
        "1. One\n7. Two\n12. Three\n\ntext\n9. New\nparagraph\n"
        "1. Parent\n   8. Child A\n   20. Child B\n9. Parent 2\n"
        "- Bullet\n5. Ordered\n* Other bullet\n10. Restart\n");
    const QString expectedLists = QStringLiteral(
        "1. One\n2. Two\n3. Three\n\ntext\n1. New\nparagraph\n"
        "1. Parent\n   1. Child A\n   2. Child B\n2. Parent 2\n"
        "- Bullet\n1. Ordered\n* Other bullet\n1. Restart\n");
    const QString renumberedLists = TextTransforms::updateListNumbers(lists);
    ok &= check(QStringLiteral("ordered, independent, nested, and mixed lists"),
                renumberedLists, expectedLists);
    ok &= check(QStringLiteral("list numbering idempotency"),
                TextTransforms::updateListNumbers(renumberedLists), expectedLists);
    ok &= check(QStringLiteral("list fenced code is untouched"),
                TextTransforms::updateListNumbers(QStringLiteral("1. Before\n```\n8. Code\n```\n9. After\n")),
                QStringLiteral("1. Before\n```\n8. Code\n```\n2. After\n"));
    ok &= check(QStringLiteral("list selection changes only selected lines"),
                TextTransforms::updateListNumbers(QStringLiteral("8. One\n9. Two\n12. Three\n"), 1, 1),
                QStringLiteral("8. One\n2. Two\n12. Three\n"));

    return ok ? 0 : 1;
}
