/*
 * Copyright (c) 2010 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/********************* WARNING !!! *********************
 *
 * PLEASE DO NOT EDIT THIS SOURCE FILE IF YOU WANT TO
 * CHANGE THE WAY ROWS ARE GROUPED. THERE IS A FUNCTION
 * IN RideNavigator.cpp CALLED groupFromValue() WHICH
 * IS CALLED TO GET THE GROUP NAME FOR A COLUMN HEADING
 * VALUE COMBINATION. THIS IS CALLED FROM whichGroup()
 * BELOW.
 *
 * Of course, if there is a bug in this ProxyModel you
 * are welcome to fix it!
 * But do take care. Specifically with the index()
 * parent() mapToSource() and mapFromSource() functions.
 *
 *******************************************************/

#ifndef _GC_RideNavigatorProxy_h
#define _GC_RideNavigatorProxy_h 1
#include "GoldenCheetah.h"

#include <QtGui>
#include "RideNavigator.h"

// Proxy model for doing groupBy
class GroupByModel : public QAbstractProxyModel
{
    Q_OBJECT
    G_OBJECT


private:
    // used to produce a ranked list
    struct rankx {
        double row;
        double value;

        bool operator< (rankx right) const {
            return (value > right.value);  // sort ascending! (.gt not .lt)
        }
        static bool sortByRow(const rankx &left, const rankx &right) {
            return (left.row < right.row); // sort ascending by row
        }
    };

    RideNavigator *rideNavigator;
    int groupBy;
    int calendarText;
    int colorColumn;
    int fileIndex;

    QList<QString> groups;
    QList<QModelIndex> groupIndexes;

    QMap<QString, QVector<int>*> groupToSourceRow;
    QVector<int> sourceRowToGroupRow;
    QList<rankx> rankedRows;

    void clearGroups() {
        // Wipe current
        QMapIterator<QString, QVector<int>*> i(groupToSourceRow);
        while (i.hasNext()) {
            i.next();
            delete i.value();
        }
        groups.clear();
        groupIndexes.clear();
        groupToSourceRow.clear();
        sourceRowToGroupRow.clear();
        rankedRows.clear();
    }


public:

    GroupByModel(RideNavigator *parent) : QAbstractProxyModel(parent), rideNavigator(parent), groupBy(-1) {
        setParent(parent);
    }
    ~GroupByModel() {}

    void setSourceModel(QAbstractItemModel *model) {
        QAbstractProxyModel::setSourceModel(model);
        setGroupBy(groupBy);

        // find the Calendar TextColumn
        calendarText = -1;
        colorColumn = -1;
        fileIndex = -1;
        for(int i=0; i<model->columnCount(); i++) {
            if (model->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString() == "filename") {
                fileIndex = i;
            }
            if (model->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString() == "color") {
                colorColumn = i;
            }
            if (model->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString() == "ZCalendar_Text") {
                calendarText = i;
            }
        }

        connect(model, SIGNAL(modelReset()), this, SLOT(sourceModelChanged()));
        connect(model, SIGNAL(dataChanged(QModelIndex, QModelIndex)), this, SLOT(sourceModelChanged()));
        connect(model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(sourceModelChanged()));
        connect(model, SIGNAL(rowsMoved(QModelIndex,int,int,QModelIndex,int)), this, SLOT(sourceModelChanged()));
        connect(model, SIGNAL(rowsRemoved(QModelIndex,int,int)), this, SLOT(sourceModelChanged()));
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const {
        if (parent.isValid()) {
            return createIndex(row,column, (void*)&groupIndexes[parent.row()]);
        } else { // XXX
            if (column == 0)
                return groupIndexes[row];
            else {
                return createIndex(row,column,(void*)NULL);
            }
        }
    }

    QModelIndex parent(const QModelIndex &index) const {
        // parent should be encoded in the index if we supplied it, if
        // we didn't then return a duffer
        if (index == QModelIndex() || index.internalPointer() == NULL) {
            return QModelIndex();
        } else if (index.column()) {
            return QModelIndex();
        }  else {
            return *static_cast<QModelIndex*>(index.internalPointer());
        }
    }

    QModelIndex mapToSource(const QModelIndex &proxyIndex) const {

        if (proxyIndex.internalPointer() != NULL) {

            int groupNo = ((QModelIndex*)proxyIndex.internalPointer())->row();
            if (groupNo < 0 || groupNo >= groups.count() || proxyIndex.column() == 0) {
                return QModelIndex();
            }

            return sourceModel()->index(groupToSourceRow.value(groups[groupNo])->at(proxyIndex.row()),
                                        proxyIndex.column()-1, // accomodate virtual column
                                        QModelIndex());
        }
        return QModelIndex();
    }

    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const {

        // which group did we put this row into?
        QString group = whichGroup(sourceIndex.row());
        int groupNo = groups.indexOf(group);

        if (groupNo < 0) {
            return QModelIndex();
        } else {
            QModelIndex *p = new QModelIndex(createIndex(groupNo, 0, (void*)NULL));
            if (sourceIndex.row() > 0 && sourceIndex.row() < sourceRowToGroupRow.size())
                return createIndex(sourceRowToGroupRow[sourceIndex.row()], sourceIndex.column()+1, &p); // accomodate virtual column
            else
                return QModelIndex();
        }
    }

    // we override the standard version to make our virtual column zero
    // selectable. If we don't do that then the arrow keys don't work
    // since there are no valid rows to cursor up or down to.
    Qt::ItemFlags flags (const QModelIndex &/*index*/) const {
        //if (index.internalPointer() == NULL) {
        //    return Qt::ItemIsEnabled;
        //} else {
            return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
        //}
    }

    QVariant data(const QModelIndex &proxyIndex, int role = Qt::DisplayRole) const {

        // this is never called ! (QT-BUG)
        if (role == Qt::TextAlignmentRole) {
            return QVariant(Qt::AlignLeft | Qt::AlignTop);
        }

        if (!proxyIndex.isValid()) return QVariant();
        QVariant returning;

        // if we are not at column 0 or we have a parent
        //if (proxyIndex.internalPointer() != NULL || proxyIndex.column() > 0) {
        if (proxyIndex.column() > 0) {

            if (role == Qt::UserRole) {

                QString string;

                if (calendarText != -1 && proxyIndex.internalPointer()) {

                    // hideous code, sorry
                    int groupNo = ((QModelIndex*)proxyIndex.internalPointer())->row();
                    if (groupNo < 0 || groupNo >= groups.count() || proxyIndex.column() == 0) returning="";
                    else string = sourceModel()->data(sourceModel()->index(groupToSourceRow.value(groups[groupNo])->at(proxyIndex.row()), calendarText)).toString();

                    // get rid of cr, lf and tab chars
                    string.replace("\n", " ");
                    string.replace("\t", " ");
                    string.replace("\r", " ");

                } else {

                    string = "";
                }

                returning = string;


            } else if (role == Qt::BackgroundRole) {


                if (colorColumn != -1 && proxyIndex.internalPointer()) {

                    QString colorstring;

                    // hideous code, sorry
                    int groupNo = ((QModelIndex*)proxyIndex.internalPointer())->row();
                    if (groupNo < 0 || groupNo >= groups.count() || proxyIndex.column() == 0)
                        colorstring="#ffffff";
                    else colorstring = sourceModel()->data(sourceModel()->index(groupToSourceRow.value(groups[groupNo])->at(proxyIndex.row()), colorColumn)).toString();

                    returning = QColor(colorstring);
                } else {
                    returning = QColor("#ffffff");
                }

            } else if (role == (Qt::UserRole+1)) {

                if (colorColumn != -1 && proxyIndex.internalPointer()) {

                    QString filename;

                    // hideous code, sorry
                    int groupNo = ((QModelIndex*)proxyIndex.internalPointer())->row();
                    if (groupNo < 0 || groupNo >= groups.count() || proxyIndex.column() == 0)
                        filename="";
                    else filename = sourceModel()->data(sourceModel()->index(groupToSourceRow.value(groups[groupNo])->at(proxyIndex.row()), fileIndex)).toString();

                    returning = filename;
                } else {
                    returning = "";
                }

            } else {

                returning = sourceModel()->data(mapToSource(proxyIndex), role);
            }

        } else if (proxyIndex.internalPointer() == NULL) {

            // its our group by!
            if (proxyIndex.row() < groups.count()) {

                // blank values become "(blank)"
                QString group = groups[proxyIndex.row()];
                if (group == "") group = QString("(blank)");

                // format the group by with ride count etc
                if (groupBy != -1) {
                    QString returnString = QString("%1: %2 (%3 activities)")
                                           .arg(sourceModel()->headerData(groupBy, Qt::Horizontal).toString())
                                           .arg(group)
                                           .arg(groupToSourceRow.value(groups[proxyIndex.row()])->count());
                    returning = QVariant(returnString);
                } else {
                    QString returnString = QString("All %1 activities")
                                           .arg(groupToSourceRow.value(groups[proxyIndex.row()])->count());
                    returning = QVariant(returnString);
                }
            }
        }

        return returning;

    }

    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole ) const {
        if (section)
            return sourceModel()->headerData(section-1, orientation, role);
        else
            return QVariant("*");
    }

    bool setHeaderData (int section, Qt::Orientation orientation, const QVariant & value, int role = Qt::EditRole) {
        if (section)
            return sourceModel()->setHeaderData(section-1, orientation, value, role);
        else
            return true;
    }

    int columnCount(const QModelIndex &/*parent*/ = QModelIndex()) const {
        return sourceModel()->columnCount(QModelIndex())+1; // accomodate virtual group column
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const {
        if (parent == QModelIndex()) {

            // top level return count of groups
            return groups.count();

        } else if (parent.column() == 0 && parent.internalPointer() == NULL) {

            // second level return count of rows for group
            return groupToSourceRow.value(groups[parent.row()])->count();

        } else {

            // no children any lower
            return 0;
        }
    }

    // does this index have children?
    bool hasChildren(const QModelIndex &index) const {

        if (index == QModelIndex()) {

            // at top
            return (groups.count() > 0);

        } else if (index.column() == 0 && index.internalPointer() == NULL) {

            // first column - the group bys
            return (groupToSourceRow.value(groups[index.row()])->count() > 0);

        } else {

            return false;

        }
    }

    void sort(int, Qt::SortOrder) {
        qDebug()<<"ASKED TO SORT!";
    }

    //
    // GroupBy features
    //
    void setGroupBy(int column) {

        // shift down
        if (column >= 0) column -= 1;

        groupBy = column; // accomodate virtual column
        setGroups();
    }

    QString whichGroup(int row) const {

        if (row == -1) return("");
        if (groupBy == -1) return tr("All Activities");
        else return groupFromValue(headerData(groupBy+1,
                                    Qt::Horizontal).toString(),
                                    sourceModel()->data(sourceModel()->index(row,groupBy)).toString(),
                                    rankedRows[row].value, rankedRows.count());

    }

    // implemented in RideNavigator.cpp, to avoid developers
    // from working out how this QAbstractProxy works, or
    // perhaps breaking it by accident ;-)
    QString groupFromValue(QString, QString, double, double) const;

    int groupCount() {
        return groups.count();
    }

    void setGroups() {

        // let the views know we're doing this
        beginResetModel();

        // wipe whatever is there first
        clearGroups();

        if (groupBy >= 0) {

            // rank all the values
            for (int i=0; i<sourceModel()->rowCount(QModelIndex()); i++) {
                rankx rank;
                rank.value = sourceModel()->data(sourceModel()->index(i,groupBy)).toDouble();
                rank.row = i;
                rankedRows << rank;
            }

            // rank the entries
            qSort(rankedRows); // sort by value
            for (int i=0; i<rankedRows.count(); i++) {
                rankedRows[i].value = i;
            }

            // sort by row again
            qStableSort(rankedRows.begin(), rankedRows.end(), rankx::sortByRow);


            // create a QMap from 'group' string to list of rows in that group
            for (int i=0; i<sourceModel()->rowCount(QModelIndex()); i++) {

                // which group are we in?
                QString value = whichGroup(i); // uses rankedRows

                QVector<int> *rows;
                if ((rows=groupToSourceRow.value(value,NULL)) == NULL) {
                    // add to list of groups
                    rows = new QVector<int>;
                    groupToSourceRow.insert(value, rows);
                }

                // rowmap is an array corresponding to each row in the
                // source model, and maps to its row # within the group
                sourceRowToGroupRow.append(rows->count());

                // add to this groups rows
                rows->append(i);
            }

        } else {

            // Just one group by 'All Rides'
            QVector<int> *rows = new QVector<int>;
            for (int i=0; i<sourceModel()->rowCount(QModelIndex()); i++) {
                rows->append(i);
                sourceRowToGroupRow.append(i);
            }
            groupToSourceRow.insert("All Activities", rows);

        }

        // Update list of groups
        int group=0;
        QMapIterator<QString, QVector<int>*> j(groupToSourceRow);
        while (j.hasNext()) {
            j.next();
            groups << j.key();
            groupIndexes << createIndex(group++,0,(void*)NULL);
        }

        // all done. let the views know everything changed
        endResetModel();
    }

public slots:
    void sourceModelChanged() {
        clearGroups();
        setGroupBy(groupBy+1);
        reset();

        // lets expand column 0 for the groupBy heading
        for (int i=0; i < groupCount(); i++)
            rideNavigator->tableView->setFirstColumnSpanned(i, QModelIndex(), true);
        // now show em
        rideNavigator->tableView->expandAll();
    }
};

// SEE QT-BUG #14831 - when it is fixed this can be removed
class BUGFIXQSortFilterProxyModel : public QSortFilterProxyModel
{

    public:

    BUGFIXQSortFilterProxyModel(QWidget *p) : QSortFilterProxyModel(p) {}

    bool lessThan(const QModelIndex &left, const QModelIndex &right) const {
        QVariant l = (left.model() ? left.model()->data(left, sortRole()) : QVariant());
        QVariant r = (right.model() ? right.model()->data(right, sortRole()) : QVariant());
        switch (l.userType()) {
            case QVariant::Invalid: return (r.type() == QVariant::Invalid);
            default: return QSortFilterProxyModel::lessThan(left, right);
        }
        return false;
    }
};
#endif