#ifndef DOCUMENTPARSER_HPP
#define DOCUMENTPARSER_HPP

#include <QObject>
#include <QString>
#include <QProcess>
#include <QTemporaryFile>

// Conditional Poppler include
#if __has_include(<poppler/qt6/poppler-qt6.h>)
    #include <poppler/qt6/poppler-qt6.h>
    #define POPPLER_AVAILABLE
    #define POPPLER_QT6
#elif __has_include(<poppler-qt6.h>)
    #include <poppler-qt6.h>
    #define POPPLER_AVAILABLE
    #define POPPLER_QT6
#elif __has_include(<poppler/qt5/poppler-qt5.h>)
    #include <poppler/qt5/poppler-qt5.h>
    #define POPPLER_AVAILABLE
#elif __has_include(<poppler-qt5.h>)
    #include <poppler-qt5.h>
    #define POPPLER_AVAILABLE
#endif

class DocumentParser : public QObject {
    Q_OBJECT
public:
    explicit DocumentParser(QObject *parent = nullptr);
    
    QString parseFile(const QString& filePath);
    
private:
    QString parsePdf(const QString& filePath);
    QString parseDocx(const QString& filePath);
    bool hasLibreOffice() const;
    QString extractTextFromHtml(const QString& htmlPath);
};

#endif
