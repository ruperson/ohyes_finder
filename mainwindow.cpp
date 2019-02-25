#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCommonStyle>
#include <QDesktopWidget>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QDirIterator>
#include <QtConcurrent>

#include <fstream>

main_window::main_window(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size(), qApp->desktop()->availableGeometry()));

    ui->treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    QCommonStyle style;
    ui->actionScan_Directory->setIcon(style.standardIcon(QCommonStyle::SP_DialogOpenButton));
    ui->actionExit->setIcon(style.standardIcon(QCommonStyle::SP_DialogCloseButton));
    ui->actionAbout->setIcon(style.standardIcon(QCommonStyle::SP_DialogHelpButton));
    ui->actionDelete->setIcon(style.standardIcon(QCommonStyle::SP_TrashIcon));

    connect(ui->actionScan_Directory, &QAction::triggered, this, &main_window::select_directory);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);
    connect(ui->actionAbout, &QAction::triggered, this, &main_window::show_about_dialog);

    qRegisterMetaType<QVector<QPair<int,int>>>("QVector<QPair<int,int>>");
    connect(this, &main_window::update_status_bar_signal, this, &main_window::update_status_bar);
    connect(this, &main_window::print_occurrence_signal, this, &main_window::print_occurrence);



    connect(ui->lineEdit, &QLineEdit::returnPressed, this, &main_window::search_directory);
    connect(this, &main_window::add_to_watcher_signal, this, &main_window::add_to_watcher);

    connect(&sw, &QFileSystemWatcher::directoryChanged, this, &main_window::sw_called);
    connect(&sw, &QFileSystemWatcher::fileChanged, this, &main_window::sw_called);
    //???
    emit update_status_bar_signal("Greetings!");
}

void main_window::search_directory() {
    if(indexed_files.empty()) {
        emit update_status_bar_signal("Index directory first");
        return;
    }
    if(ui->lineEdit->text().size() < 3) {
        emit update_status_bar_signal("Please enter at least 3 characters to perform search");
        return;
    }
    emit update_status_bar_signal("Searching in the files");
    ui->treeWidget->clear();
    for (auto x : indexed_files) {
        QtConcurrent::run ([this, x]() {search_file(x, ui->lineEdit->text());});
    }

    emit update_status_bar_signal("Occurence finding has finished");
}


int boyerMoore(std::string const& text, std::string const& pattern) {
    int ans = 0;
    auto it = std::search(text.begin(), text.end(),std::boyer_moore_searcher(pattern.begin(), pattern.end()));
    while (it != text.end()) {
        ans++;
        it = std::search(it + 1, text.end(),std::boyer_moore_searcher(pattern.begin(), pattern.end()));
    }
    return ans;
}

void main_window::search_file(indexed_file file, QString searchedStr) {
    QString trigr = searchedStr.left(2);
    for(int i = 2; i < searchedStr.size(); i++) {
        trigr.append(searchedStr.at(i));
        if(!file.trigrams.count(trigram(trigr.at(0).toLatin1(), trigr.at(1).toLatin1(), trigr.at(2).toLatin1()))) return;
        trigr = trigr.right(2);
    }

    QVector<QPair<int, int>> ans;

    std::ifstream in(file.file.toStdString());
    if (!in.is_open()) {
        return;
    }
    std::string cur;
    std::string pat = searchedStr.toStdString();
    int number = 0;
    while (!in.eof()) {
        number++;
        std::getline(in, cur);
        int count = boyerMoore(cur, pat);
        if (count > 0) {
            ans.push_back({count, number});
        }
    }
    if (!ans.empty()) {
        emit print_occurrence_signal(file.file, ans);
    }
}



main_window::~main_window()
{}

void main_window::update_status_bar(QString  message) {
    ui->statusBar->showMessage(message);
}

void main_window::select_directory()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Directory for Scanning",
                                                    QString(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty() && !dir.isNull()) {
        scan_directory(dir);
    }
}

void main_window::print_occurrence(QString str, QVector<QPair<int, int>> data) {
    QTreeWidgetItem* item = new QTreeWidgetItem(ui->treeWidget);
    item->setText(0, str);
    for (auto row : data) {
       QTreeWidgetItem* child = new QTreeWidgetItem();
       child->setText(0, QString::number (row.first) + " times" );
       child->setText(1, QString::number (row.second) );
       item->addChild(child);
    }
}

bool validUTF8(unsigned char c1, unsigned char c2) {
    if (c1 < 0x80) // 1-byte, must be followed by 1-byte or first of multi-byte
        return c2 < 0x80 || (0xc0 <= c2 && c2 < 0xf8);
    if (c1 < 0xc0) // continuation byte, can be followed by nearly anything
        return c2 < 0xf8;
    if (c1 < 0xf8) // first of multi-byte, must be followed by continuation byte
        return 0x80 <= c2 && c2 < 0xc0;

    return false;
}


void main_window::scan_directory(QString const& dir) {
    ui->treeWidget->clear();
    emit update_status_bar_signal("Indexing . . .");
    setWindowTitle(QString("Index for dir  %1").arg(dir));
    QTime timer;
    timer.start();
    emit add_to_watcher_signal(dir);
    QtConcurrent::run([dir, timer, this]() {
        QDir d(dir);
        QDirIterator it(dir, QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks | QDir::AllEntries, QDirIterator::Subdirectories);
        while(it.hasNext()) {
            QString filePath = it.next();
            if (it.fileInfo().isDir() || it.fileInfo().size() > (1<<30)) {
                emit add_to_watcher_signal(filePath);
                notindexedbutwatched.push_back(filePath);
                continue;
            }
            QFile file(filePath);
            file.open(QFile::ReadOnly);
            QByteArray str = file.read(2);
            std::set<trigram> trigrams;
            bool valid = true;
            while(valid && !file.atEnd()) {
                QByteArray buffer = file.read(8192);
                for(int i = 0; i < buffer.size(); i++) {
                    str.append(buffer.at(i));
                    valid = validUTF8(static_cast<unsigned char>(str.at(0)),static_cast<unsigned char>(str.at(1)))
                            && validUTF8(static_cast<unsigned char>(str.at(1)), static_cast<unsigned char>(str.at(2)));
                    trigrams.emplace(str.at(0), str.at(1), str.at(2));
                    str = str.right(2);
                }
            }
            if (valid) {
                emit add_to_watcher_signal(filePath);
                indexed_files.emplace_back(filePath, trigrams);
            }
        }
        emit update_status_bar_signal(QString("Indexing finished in ") + QString::number(timer.elapsed() / 1000.0) + QString(" sec"));
    });
}

void main_window::add_to_watcher(QString filePath) {
    sw.addPath(filePath);
}

void main_window::sw_called(QString f) {
    qDebug() << "This one was changed : " << f;
    for(auto it: indexed_files){
        sw.removePath(it.file);
    }
    for (auto x : notindexedbutwatched) {
        sw.removePath(x);
    }
    emit update_status_bar_signal("Watcher detected a change, please rescan !");
    indexed_files.clear();
    ui->treeWidget->clear();
}

void main_window::show_about_dialog()
{
    QMessageBox::aboutQt(this);
}
