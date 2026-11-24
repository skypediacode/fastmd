#include <QApplication>
#include <QPlainTextEdit>
#include <QMimeData>
#include <QClipboard>
#include <QDebug>
#include ""MarkdownEditor.h""

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MarkdownEditor editor;
    QClipboard *clipboard = QGuiApplication::clipboard();
    QMimeData *mime = new QMimeData;
    mime->setText(""Hello World"");
    clipboard->setMimeData(mime);
    
    if (editor.canInsertFromMimeData(clipboard->mimeData())) {
        editor.insertFromMimeData(clipboard->mimeData());
        qDebug() << ""Pasted text:"" << editor.toPlainText();
    } else {
        qDebug() << ""Cannot paste text"";
    }
    return 0;
}
