#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory>

#include <QLineEdit>
#include <QFileSystemWatcher>
#include <set>
#include <vector>
struct trigram {
    char a,b,c;
    trigram(char A, char B, char C) : a(A), b(B), c(C) {}
    bool operator<(trigram const & other) const {
        if (a != other.a) return a < other.a;
        if (b != other.b) return b < other.b;
        return c < other.c;
    }
};

struct indexed_file {
    indexed_file(QString const & a, std::set<trigram> const & b) : file(a), trigrams(b) {}
    QString file;
    std::set<trigram> trigrams;
};

namespace Ui {
class MainWindow;
}

class main_window : public QMainWindow
{
    Q_OBJECT

public:
    explicit main_window(QWidget *parent = 0);
    ~main_window();

private slots:
    void select_directory();
    void scan_directory(QString const&);
    void show_about_dialog();
    void update_status_bar(QString);

    void search_directory();
    void search_file(indexed_file, QString);


    void add_to_watcher(QString);
    void sw_called(QString);

    void print_occurrence(QString, QVector<QPair<int, int>>);

signals:
    void update_status_bar_signal(QString);

    void add_to_watcher_signal(QString);

    void print_occurrence_signal(QString, QVector<QPair<int, int>>);

private:
    std::unique_ptr<Ui::MainWindow> ui;
    std::vector<indexed_file> indexed_files;
    std::vector <QString> notindexedbutwatched;
    QFileSystemWatcher sw;
};

#endif // MAINWINDOW_H
