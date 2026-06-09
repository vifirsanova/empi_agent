#include "DocumentParser.hpp"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include <QTemporaryDir>

DocumentParser::DocumentParser(QObject *parent) 
    : QObject(parent) {}

QString DocumentParser::parseFile(const QString& filePath) {
    QFileInfo info(filePath);
    QString ext = info.suffix().toLower();
    
    if (ext == "pdf") {
        return parsePdf(filePath);
    } 
    else if (ext == "docx" || ext == "doc") {
        return parseDocx(filePath);
    }
    else if (ext == "txt" || ext == "html" || ext == "htm" || ext == "md") {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            QTextStream stream(&file);
            return stream.readAll();
        }
        return "Error: Cannot open text file";
    }
    
    return QString("Error: Unsupported file format: %1 (supported: PDF, DOCX, DOC, TXT, HTML, MD)").arg(ext);
}

QString DocumentParser::parsePdf(const QString& filePath) {
#ifdef POPPLER_AVAILABLE
    auto document = Poppler::Document::load(filePath);
    
    if (!document || document->isLocked()) {
        return "Error: Cannot open PDF file (possibly password protected or corrupted)";
    }
    
    QString allText;
    int numPages = document->numPages();
    
    for (int i = 0; i < numPages; ++i) {
        auto page = document->page(i);
        if (page) {
#ifdef POPPLER_QT6
            QString pageText = page->text(QRectF());
#else
            QString pageText = page->text(Poppler::Page::PhysicalLayout);
#endif
            if (!pageText.isEmpty()) {
                allText += pageText + "\n\n";
            }
        }
    }
    
    if (allText.isEmpty()) {
        return "Error: No text content extracted from PDF (may be image-based or scanned)";
    }
    
    return allText;
#else
    QProcess process;
    process.start("pdftotext", QStringList() << filePath << "-");
    
    if (!process.waitForFinished(30000)) {
        return "Error: pdftotext timeout (30 seconds)";
    }
    
    if (process.exitCode() != 0) {
        return "Error: Failed to extract PDF text. Install poppler-utils or poppler-qt6.";
    }
    
    QString output = QString::fromUtf8(process.readAllStandardOutput());
    if (output.isEmpty()) {
        return "Error: No text extracted from PDF";
    }
    
    return output;
#endif
}

bool DocumentParser::hasLibreOffice() const {
    QProcess process;
    process.start("soffice", QStringList() << "--version");
    process.waitForFinished(3000);
    return process.exitCode() == 0;
}

QString DocumentParser::parseDocx(const QString& filePath) {
    if (!hasLibreOffice()) {
        return "Error: LibreOffice not installed. Please install LibreOffice to parse DOCX/DOC files.\n"
               "Linux: sudo apt install libreoffice\n"
               "macOS: brew install libreoffice\n"
               "Windows: Download from libreoffice.org";
    }
    
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return "Error: Cannot create temporary directory";
    }
    
    QProcess process;
    process.start("soffice", QStringList() 
                  << "--headless"
                  << "--convert-to" << "html"
                  << "--outdir" << tempDir.path()
                  << filePath);
    
    if (!process.waitForFinished(30000)) {
        process.kill();
        return "Error: LibreOffice conversion timeout (30 seconds)";
    }
    
    if (process.exitCode() != 0) {
        QString error = QString::fromUtf8(process.readAllStandardError());
        return QString("Error: LibreOffice conversion failed: %1").arg(error);
    }
    
    QFileInfo inputInfo(filePath);
    QString baseName = inputInfo.completeBaseName();
    QString htmlPath = tempDir.path() + "/" + baseName + ".html";
    
    return extractTextFromHtml(htmlPath);
}

QString DocumentParser::extractTextFromHtml(const QString& htmlPath) {
    QFile file(htmlPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return "Error: Cannot read converted HTML file";
    }
    
    QString html = QString::fromUtf8(file.readAll());
    file.close();
    
    QString text;
    bool inTag = false;
    bool inScript = false;
    
    for (int i = 0; i < html.length(); ++i) {
        QChar ch = html[i];
        
        if (ch == '<') {
            inTag = true;
            if (html.mid(i, 7).toLower() == "<script" ||
                html.mid(i, 6).toLower() == "<style") {
                inScript = true;
            }
            continue;
        }
        
        if (ch == '>') {
            inTag = false;
            if (inScript && (html.mid(i-8, 9).toLower() == "</script>" ||
                            html.mid(i-7, 8).toLower() == "</style>")) {
                inScript = false;
            }
            if (i > 0 && html[i-1] == '>') {
                QString tag = html.mid(html.lastIndexOf('<', i-1), i - html.lastIndexOf('<', i-1));
                if (tag.contains("p") || tag.contains("div") || tag.contains("br") || tag.contains("h")) {
                    text += "\n\n";
                }
            }
            continue;
        }
        
        if (!inTag && !inScript && !ch.isSpace()) {
            text += ch;
        } else if (!inTag && !inScript && ch == ' ' && !text.endsWith(' ')) {
            text += ' ';
        }
    }
    
    text = text.simplified();
    
    if (text.isEmpty()) {
        return "Warning: No text content extracted from DOCX file";
    }
    
    return text;
}
