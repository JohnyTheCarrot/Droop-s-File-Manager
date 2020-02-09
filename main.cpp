#include <iostream>
#include <string>
#include <stdio.h>
#include <curses.h>
#include <ncurses.h>
#include <stdlib.h>
#include <array>
#include <bits/stdc++.h>
#include <filesystem>
#include <unistd.h>
#include <fstream>
#include <streambuf>
namespace fs = std::filesystem;

// Compile with:
// g++ main.cpp -o main -std=c++17 -lncurses && ./main

enum SortBy {
    FILENAME,
    FILESIZE
};

enum SortMode {
    ASCENDING,
    DESCENDING
};

enum OptionsBarType {
    BARTYPE_FOLDER,
    BARTYPE_FILE,
    BARTYPE_SIMPLE
};

enum InputType {
    COMMAND,
    RENAME,
    NONE
};

struct SortHeader {
    SortBy sortBy;
    SortMode sortMode;
};

SortHeader sortByFilenameHeader = { FILENAME, DESCENDING };
SortHeader sortByFilesizeHeader = { FILESIZE, DESCENDING };

WINDOW *treeView;
WINDOW *mainView;

std::string appName = "Droop's File Manager";

// function keys
const int funSortByFilename = 1;
const int funSortByFilesize = 2;
const int funSortByModified = 3;
const int funOptionsBarRename = 4;
const int funOptionsBarRenameSelected = 5;
const int funOptionsBarViewSelected = 6;
// widths, heights, etc
int treeViewWidth = 25;
int treeViewHeight;
int treeViewElementSpacing = 3;
int treeViewElementHeight = 3;
int treeViewElementMargin = 1;
int mainViewHeight;
int mainViewSimpleLeftMargin = 3;
int mainViewScrollOffset = 0;
int mainViewScrollIndex = 0;
int mainViewSelectedIndex = 0;
bool mainViewSelectedIsHeader = false;
bool mainViewCanScrollDown = false, mainViewCanScrollUp = false, mainViewCanMoveDown = false, mainViewCanMoveUp = false;
int mainViewContentStart = 2, mainViewContentEnd;
int mainViewHeaderAdditionalSpacing = 2;
// file view
int fileViewScrollOffset = 0;
int fileViewStart = 1;
int fileViewEnd;
int fileViewMarginLeft = 2;
bool isViewingFile = false;
// input
int cursorPosX = 2, cursorPosY;
std::string input = "";
bool isTyping = false;
bool systemCallSupported = system(NULL) != 0;
bool isShowingOutput = false;
InputType currentInputType;
// file options bar
int optionsBarMarginLeft = 1;
int optionsBarSpacing = 2;
bool optionsBarOpen = false;
OptionsBarType optionsBarType;

SortMode currentSortMode = DESCENDING;
SortBy currentSortBy = FILENAME;
std::filesystem::__cxx11::path currentPath = "";

struct treeElement
{
    std::string path;
    std::string name;
    bool selected;
    int y;
};

std::array<treeElement, 1> treeElements = {{
    {fs::current_path().string(), fs::current_path().filename(), false}
}};

void showOptionsBar(OptionsBarType type)
{
    std::string bar = std::string(optionsBarMarginLeft, ' ');
    optionsBarType = type;
    bar += "F" + std::to_string(funOptionsBarRename) + " Rename Current Folder";
    if (optionsBarType != BARTYPE_SIMPLE)
    {
        bar += std::string(optionsBarSpacing, ' ') + "F" + std::to_string(funOptionsBarRenameSelected) + " Rename Selected";
    }
    if (optionsBarType == BARTYPE_FILE)
    {
        bar += std::string(optionsBarSpacing, ' ') + "F" + std::to_string(funOptionsBarViewSelected) + " View Selected";
    }
    attron(A_STANDOUT);
    mvaddstr(LINES - 2, 0, std::string(COLS, ' ').c_str());
    mvaddstr(LINES - 2, 0, bar.c_str());
    attroff(A_STANDOUT);
    refresh();
}

int displayGroup(
    std::vector<std::filesystem::__cxx11::path> toDisplay,
    int index,
    std::string name,
    int longestNameLength
)
{
    std::string arrow = "-> ";
    if (toDisplay.size() > 0)
    {
        if (index >= mainViewContentStart && index < mainViewContentEnd)
        {
            wattron(mainView, A_STANDOUT);
            if (mainViewScrollIndex == index - mainViewContentStart)
            {
                wattron(mainView, A_ITALIC);
                mainViewSelectedIndex = index - mainViewContentStart;
                mainViewSelectedIsHeader = true;
                optionsBarType = BARTYPE_SIMPLE;
                showOptionsBar(BARTYPE_SIMPLE);
            }
            std::string header = "v " + name;
            mvwaddstr(mainView, index, 2, (header + std::string(COLS - treeViewWidth - header.size() - 4, ' ')).c_str());
            wattroff(mainView, A_STANDOUT);
            if (mainViewScrollIndex == index - mainViewContentStart)
                wattroff(mainView, A_ITALIC);
        }
        index++;
        for (const auto &folderOrFile : toDisplay)
        {
            if (index >= mainViewContentStart && index < mainViewContentEnd)
            {
                if (mainViewScrollIndex == index - mainViewContentStart)
                {
                    mainViewSelectedIndex = index - mainViewContentStart;
                    mainViewSelectedIsHeader = false;
                    showOptionsBar(fs::is_directory(folderOrFile) ? BARTYPE_FOLDER : BARTYPE_FILE);
                }
                std::string folderOrFileName = folderOrFile.filename().string();
                int spacing = longestNameLength - folderOrFileName.size() + mainViewHeaderAdditionalSpacing;
                std::string filesize = "";
                if (!fs::is_directory(folderOrFile) && fs::exists(folderOrFile))
                    filesize = std::to_string(fs::file_size(folderOrFile));
                std::string folderOrFileEntry = folderOrFileName + std::string(spacing, ' ') + filesize;
                mvwaddstr(
                    mainView,
                    index,
                    2, 
                    ((mainViewScrollIndex == index - mainViewContentStart ? arrow : std::string(mainViewSimpleLeftMargin, ' ')) + folderOrFileEntry).c_str()
                );
            }
            index++;
        }
    }
    return index;
}

bool sort(std::filesystem::__cxx11::path p1, std::filesystem::__cxx11::path p2)
{
    if (currentSortBy == FILENAME || fs::is_directory(p1) || fs::is_directory(p2))
        return (sortByFilenameHeader.sortMode == DESCENDING ? p1.string() < p2.string() : p1.string() > p2.string());
    else if (currentSortBy == FILESIZE)
        return (sortByFilesizeHeader.sortMode == DESCENDING ? fs::file_size(p1) < fs::file_size(p2) : fs::file_size(p1) > fs::file_size(p2));
    else return (sortByFilenameHeader.sortMode == DESCENDING ? p1.string() < p2.string() : p1.string() > p2.string());
}

void displayPathContentsSimple(std::string path, SortBy sortBy = FILENAME, SortMode sortMode = DESCENDING)
{
    std::vector<std::filesystem::__cxx11::path> foldersToDisplay;
    std::vector<std::filesystem::__cxx11::path> filesToDisplay;
    int longestFoldernameLength = 0;
    int longestFilenameLength = 0;
    for (const auto &entry : fs::directory_iterator(path))
    {
        if (!fs::exists(entry.path())) continue;
        int nameLength = entry.path().filename().string().size();
        if (fs::is_directory(entry.path()))
        {
            if (nameLength > longestFoldernameLength)
                longestFoldernameLength = nameLength;
            foldersToDisplay.push_back(entry.path());
        }
        else
        {
            if (nameLength > longestFilenameLength)
                longestFilenameLength = nameLength;
            filesToDisplay.push_back(entry.path());
        }
    }
    std::sort(foldersToDisplay.begin(), foldersToDisplay.end(), sort);
    std::sort(filesToDisplay.begin(), filesToDisplay.end(), sort);
    int index = mainViewContentStart - mainViewScrollOffset;
    std::string sortingLegendFilename = (sortByFilenameHeader.sortMode == DESCENDING ? std::string(1, 'V') : std::string(1, '^')) + " F1 Filename";
    std::string sortingLegendFilesize = (sortByFilesizeHeader.sortMode == DESCENDING ? std::string(1, 'V') : std::string(1, '^')) + " F2 Filesize";
    /* This is so if the file-/folder- names are smaller than the longestFoldernameLength,
     * it will base the spacing on the longestFoldernameLength size, instead of the size of the longest name
     */
    if (sortingLegendFilename.size() > longestFilenameLength)
        longestFilenameLength = sortingLegendFilename.size();
    if (sortingLegendFilename.size() > longestFoldernameLength)
        longestFoldernameLength = sortingLegendFilename.size();
    int folderOrFilenameLength = longestFoldernameLength > longestFilenameLength ? longestFoldernameLength : longestFilenameLength;
    std::string sortingLegend = std::string(mainViewSimpleLeftMargin, ' ') + sortingLegendFilename;
    sortingLegend += std::string(folderOrFilenameLength - sortingLegendFilename.size() + mainViewHeaderAdditionalSpacing, ' ') + sortingLegendFilesize;
    wattron(mainView, A_STANDOUT);
    mvwaddstr(
        mainView, 
        mainViewContentStart-1, 
        2, 
        (sortingLegend + std::string(COLS - treeViewWidth - sortingLegend.size() - 4, ' ')).c_str());
    wattroff(mainView, A_STANDOUT);
    index = displayGroup(foldersToDisplay, index, "Folders", longestFoldernameLength);
    index = displayGroup(filesToDisplay, index, "Files", longestFilenameLength);
    // if anything was found, the index should've changed by now
    if (index == mainViewContentStart - mainViewScrollOffset)
    {
        mvwaddstr(mainView, index, 2, "It's empty in here..");
    }
    mainViewCanScrollDown = index > mainViewContentEnd;
    mainViewCanScrollUp = mainViewContentStart - mainViewScrollOffset < mainViewScrollIndex + 1;
    mainViewCanMoveDown = mainViewScrollIndex < index - mainViewContentStart - 1;
    mainViewCanMoveUp = mainViewScrollIndex > -1;
    if (mainViewContentEnd - mainViewContentStart - 1 <= mainViewScrollIndex && mainViewCanScrollDown)
    {
        mainViewScrollOffset++;
        mainViewScrollIndex--;
    }
    if (mainViewContentStart - 1 >= mainViewScrollIndex && mainViewCanScrollUp)
    {
        mainViewScrollOffset--;
        mainViewScrollIndex++;
    }
}

void resetMainView()
{
    mainView = newwin(mainViewHeight, COLS - treeViewWidth, 1, treeViewWidth);
    box(mainView, 0, 0);
}

void resetAndRefreshMainView()
{
    resetMainView();
    displayPathContentsSimple(currentPath.string());
    wrefresh(mainView);
    move(cursorPosY, cursorPosX);
}

void openFavorite()
{
    mainViewSelectedIndex = 0;
    for (int i = 0; i < treeElements.size(); i++)
    {
        if (treeElements[i].selected)
        {
            currentPath = treeElements[i].path;
            displayPathContentsSimple(treeElements[i].path);
            break;
        }
    }
    wrefresh(mainView);
}

void initMainView()
{
    resetMainView();
    move(cursorPosY, cursorPosX);
    wrefresh(mainView);
}

void enterDirectory(std::filesystem::__cxx11::path path)
{
    currentPath = path;
    mainViewScrollIndex = 0;
    mainViewScrollOffset = 0;
    mainViewSelectedIndex = 0;
    mvprintw(0, treeViewWidth, std::string(COLS - treeViewWidth, ' ').c_str());
    wrefresh(mainView);
    std::string title = currentPath.filename();
    mvprintw(0, (COLS - title.size()) / 2, title.c_str());
    resetAndRefreshMainView();
}

// TODO: simplify following two functions into one
std::filesystem::__cxx11::path getSelectedFile()
{
    std::vector<std::filesystem::__cxx11::path> foldersToDisplay;
    std::vector<std::filesystem::__cxx11::path> filesToDisplay;
    for (const auto &entry : fs::directory_iterator(currentPath.string()))
    {
        if (!fs::exists(entry.path())) continue;
        if (fs::is_directory(entry.path()))
            foldersToDisplay.push_back(entry.path());
        else
            filesToDisplay.push_back(entry.path());
    }
    // the first list displayed are the folders. Add 2 to that because there's two headers.
    int index = foldersToDisplay.size() + 2;
    std::sort(filesToDisplay.begin(), filesToDisplay.end(), sort);
    for (const auto &entry : filesToDisplay)
    {
        if (mainViewSelectedIndex == index - mainViewScrollOffset)
        {
            return entry;
        }
        index++;
    }
}

std::filesystem::__cxx11::path getSelectedFolder()
{
    int index = 1;
    std::vector<std::filesystem::__cxx11::path> foldersToDisplay;
    std::vector<std::filesystem::__cxx11::path> filesToDisplay;
    for (const auto &entry : fs::directory_iterator(currentPath.string()))
    {
        if (!fs::exists(entry.path())) continue;
        if (fs::is_directory(entry.path()))
            foldersToDisplay.push_back(entry.path());
        else
            filesToDisplay.push_back(entry.path());
    }
    std::sort(foldersToDisplay.begin(), foldersToDisplay.end(), sort);
    for (const auto &entry : foldersToDisplay)
    {
        if (mainViewSelectedIndex == index - mainViewScrollOffset)
        {
            return entry;
        }
        index++;
    }
}

void enterSelectedDirectory()
{
    enterDirectory(getSelectedFolder());
}

void initTreeView()
{
    move(0,0);
    treeView = newwin(treeViewHeight, treeViewWidth, 0, 0);
    box(treeView, 0, 0);
    keypad(stdscr, TRUE);
    mvwaddstr(treeView, 1, 2, "Favorites");
    for (int i = 0; i < treeElements.size(); i++)
    {
        std::string name = treeElements[i].name;
        if (treeElements[i].selected)
            name = "-> " + name;
        treeElements[i].y = (i + 1) * treeViewElementSpacing;
        mvwaddstr(treeView, (i + 1) * treeViewElementSpacing, 2, ("+" + std::string(treeViewWidth - 6, '-') + "+").c_str());
        mvwaddstr(
            treeView,
            ((i + 1) * treeViewElementSpacing) + 1,
            2,
            ("|" + std::string(" ", treeViewElementMargin) + name + std::string(treeViewWidth - 6 - treeViewElementMargin - name.size(), ' ') + "|").c_str());
        mvwaddstr(treeView, ((i + 1) * treeViewElementSpacing) + 2, 2, ("+" + std::string(treeViewWidth - 6, '-') + "+").c_str());
        wrefresh(treeView);
    }
    wrefresh(treeView);
}

void selectTreeElement(int element)
{
    if (treeElements[element].selected)
        return;
    mainViewScrollIndex = 0;
    mainViewScrollOffset = 0;
    treeElements[element].selected = true;
    currentPath = treeElements[element].path;
    clear();
    std::string title = treeElements[element].name;
    mvprintw(0, (COLS - title.size()) / 2, title.c_str());
    refresh();
    openFavorite();
    initMainView();
}

void unselectTreeElement(int element)
{
    treeElements[element].selected = false;
}

void init()
{
    initscr();
    cbreak();
    noecho();

    if ((LINES < 24) || (COLS < 80))
    {
        endwin();
        puts("Your terminal needs to be at least 80x24");
        exit(2);
    }

    clear();
    treeViewHeight = LINES - 2;
    mainViewHeight = LINES - 3;
    mainViewContentEnd = mainViewHeight - 1;
    cursorPosY = LINES - 1;
    fileViewEnd = LINES - 2;
}

void drawInputContent()
{
    attron(A_STANDOUT);
    mvaddstr(cursorPosY, 0, std::string(COLS, ' ').c_str());
    if (systemCallSupported || currentInputType == RENAME)
    {
        std::string prompt;
        switch (currentInputType)
        {
            case COMMAND:
                prompt = "COMMAND >: ";
                break;
            case RENAME:
                prompt = "RENAME >: ";
                break;
        }
        mvaddstr(cursorPosY, cursorPosX, (prompt + input).c_str());
    }
    else
        mvaddstr(cursorPosY, cursorPosX, "System call unsupported on system. Commands will not work, sorry.");
    attroff(A_STANDOUT);
    refresh();
}

void clearCommandContent()
{
    mvaddstr(cursorPosY, 0, std::string(COLS, ' ').c_str());
    refresh();
}

// Convert ANSI escape sequences to attributes
void ansiToAttr(int y, int x, std::string str)
{
    int relativeX = 0;
    for(int i = 0; i < str.size(); i++)
    {
        char c = str.at(i);
        if (c == '\033')
        {
            for (int i1 = i; i1 < str.size() - i; i1++)
            {
                if (str.at(i1) == 'm' || str.at(i1) == 's' || str.at(i1) == 'A')
                {
                    relativeX += i1 - i;
                    i = i1;
                    break;
                }
            }
        }
        mvaddch(y,x+relativeX, c);
        refresh();
        relativeX++;
    }
}

// Code by gregpatron08:
// https://stackoverflow.com/questions/478898/how-do-i-execute-a-command-and-get-the-output-of-the-command-within-c-using-po
void exec(const char* cmd) {
    def_prog_mode(); /* save current tty modes */
    endwin();
    std::cout << "OUTPUT:" << std::endl;
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    int index = 1;
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        std::cout << buffer.data();
        index++;
    }
}

void promptForRenameOnSelected()
{
    currentInputType = RENAME;
    drawInputContent();
    isTyping = !isTyping;
}

void renameSelected()
{
    fs::path path;
    if (optionsBarType == BARTYPE_FILE)
        path = getSelectedFile();
    else if (optionsBarType == BARTYPE_FOLDER)
        path = getSelectedFolder();
    fs::rename(path, path.parent_path()/input);
}

void signal_callback_handler(int signum)
{
    if (isShowingOutput)
    {
        refresh();
        mvaddstr(LINES - 1, 0, std::string(COLS, ' ').c_str());
        isShowingOutput = false;
    }
    else
    {
        clear();
        endwin();
        std::cout << "Bye for now! Thanks for using " << appName << " :)" << std::endl;
        exit(0);
    }
}

void viewFile()
{
    clear();
    attron(A_STANDOUT);
    mvaddstr(0, 0, std::string(COLS, ' ').c_str());
    mvaddstr(0, 0, "File Contents:");
    attroff(A_STANDOUT);
    std::ifstream file(getSelectedFile().c_str());
    //std::string content( (std::istreambuf_iterator<char>(ifs) ), (std::istreambuf_iterator<char>()    ) );
    //mvaddstr(1-fileViewScrollOffset, 0, content.c_str());
    std::string line;
    int index = fileViewStart;
    int lineNumber = 0;
    while (std::getline(file, line))
    {
        lineNumber++;
        if (index - fileViewScrollOffset >= fileViewStart && index - fileViewScrollOffset < fileViewEnd)
            mvaddstr(index - fileViewScrollOffset, 0, (std::to_string(lineNumber) + std::string(fileViewMarginLeft, ' ') + line).c_str());
        index++;
    }
    mvaddstr(LINES - 1, 0, "Continue [ENTER]");
    isShowingOutput = true;
    isViewingFile = true;
}

int main(void)
{
    signal(SIGINT, signal_callback_handler);
    init();
    selectTreeElement(0);
    initTreeView();
    initMainView();
    openFavorite();
    int treeCode;
    mousemask(BUTTON1_PRESSED | BUTTON2_PRESSED, NULL);
    while (1)
    {
        treeCode = getch();
        MEVENT event;
        switch (treeCode)
        {
            case KEY_RESIZE:
                init();
                refresh();
                if (isViewingFile) 
                {
                    viewFile();
                    break;
                }
                initTreeView();
                resetAndRefreshMainView();
                break;
            case KEY_MOUSE:
                if (getmouse(&event) == OK)
                {
                    if (event.bstate & BUTTON1_PRESSED)
                    {
                        bool didNavigate = false;
                        for (int i = 0; i < treeElements.size(); i++)
                        {
                            if (event.x < treeViewWidth)
                            {
                                if (event.y >= treeElements[i].y && event.y < treeElements[i].y + treeViewElementHeight)
                                {
                                    selectTreeElement(i);
                                    didNavigate = true;
                                }
                                else
                                    unselectTreeElement(i);
                            }
                        }
                        if (didNavigate) initTreeView();
                    }
                }
                break;
            case 10:
                if (isShowingOutput)
                {
                    // close output view
                    refresh();
                    mvaddstr(0, treeViewWidth, std::string(COLS - treeViewWidth, ' ').c_str());
                    mvaddstr(LINES - 1, 0, std::string(COLS, ' ').c_str());
                    std::string title = currentPath.filename();
                    mvprintw(0, (COLS - title.size()) / 2, title.c_str());
                    initTreeView();
                    resetAndRefreshMainView();
                    isShowingOutput = false;
                    isViewingFile = false;
                    fileViewScrollOffset = 0;
                }
                else if (!isTyping)
                    enterSelectedDirectory();
                else if (isTyping && !systemCallSupported)
                    clearCommandContent();
                else if (isTyping && currentInputType == COMMAND)
                {
                    isShowingOutput = true;
                    exec(("cd " + currentPath.string() + " && " + input).c_str());
                    isTyping = false;
                    currentInputType = NONE;
                    input = "";
                } else if (isTyping && currentInputType == RENAME)
                {
                    renameSelected();
                    mvaddstr(LINES - 1, 0, std::string(COLS, ' ').c_str());
                    refresh();
                    resetAndRefreshMainView();
                    isTyping = false;
                    input = "";
                    currentInputType = NONE;
                }
                break;
            case KEY_BACKSPACE:
                if (isShowingOutput) break;
                if (!isTyping)
                    enterDirectory(currentPath.parent_path());
                else
                {
                    if (input.size() > 0)
                    {
                        input.pop_back();
                        drawInputContent();
                    }
                }
                break;
            case KEY_DOWN:
                if (isShowingOutput) {
                    fileViewScrollOffset++;
                    viewFile();
                    break;
                }
                if (mainViewCanMoveDown)
                    mainViewScrollIndex++;
                resetAndRefreshMainView();
                break;
            case KEY_UP:
                if (isShowingOutput) {
                    fileViewScrollOffset--;
                    viewFile();
                    break;
                }
                if (mainViewCanMoveUp)
                    mainViewScrollIndex--;
                resetAndRefreshMainView();
                break;
            /**case KEY_F(5):
                resetAndRefreshMainView();
                break;*/
            // sorting
            case KEY_F(funSortByFilename):
                if (currentSortBy == FILENAME)
                    sortByFilenameHeader.sortMode = (sortByFilenameHeader.sortMode == ASCENDING ? DESCENDING : ASCENDING);
                currentSortBy = FILENAME;
                resetAndRefreshMainView();
                break;
            case KEY_F(funSortByFilesize):
                if (currentSortBy == FILESIZE)
                    sortByFilesizeHeader.sortMode = (sortByFilesizeHeader.sortMode == ASCENDING ? DESCENDING : ASCENDING);
                currentSortBy = FILESIZE;
                resetAndRefreshMainView();
                break;
            // folder/file commands
            case KEY_F(funOptionsBarRenameSelected):
                promptForRenameOnSelected();
                break;
            case KEY_F(funOptionsBarViewSelected):
                if (optionsBarType == BARTYPE_FILE)
                {
                    viewFile();
                    /**clear();
                    attron(A_STANDOUT);
                    mvaddstr(0, 0, std::string(COLS, ' ').c_str());
                    mvaddstr(0, 0, "File Contents:");
                    attroff(A_STANDOUT);
                    std::ifstream ifs(getSelectedFile().c_str());
                    std::string content( (std::istreambuf_iterator<char>(ifs) ),
                       (std::istreambuf_iterator<char>()    ) );
                    mvaddstr(1, 0, content.c_str());
                    mvaddstr(LINES - 1, 0, "Continue [ENTER]");
                    isShowingOutput = true;*/
                }
                break;
            // command typing
            case (int)':':
                if (input.size() == 0)
                {
                    currentInputType = COMMAND;
                    drawInputContent();
                    isTyping = !isTyping;
                    break;
                }
            default:
                if (isTyping)
                {
                    input += (char)treeCode;
                    drawInputContent();
                }
                break;
        }
    }
    return 0;
}