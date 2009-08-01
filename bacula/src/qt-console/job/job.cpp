/*
   Bacula® - The Network Backup Solution

   Copyright (C) 2007-2009 Free Software Foundation Europe e.V.

   The main author of Bacula is Kern Sibbald, with contributions from
   many others, a complete list can be found in the file AUTHORS.
   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version two of the GNU General Public
   License as published by the Free Software Foundation and included
   in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.

   Bacula® is a registered trademark of Kern Sibbald.
   The licensor of Bacula is the Free Software Foundation Europe
   (FSFE), Fiduciary Program, Sumatrastrasse 25, 8006 Zürich,
   Switzerland, email:ftf@fsfeurope.org.
*/
 
/*
 *   Version $Id$
 *
 *  Job Class
 *
 *   Dirk Bartley, March 2007
 *
 */ 

#include "bat.h"
#include "job.h"
#include "util/fmtwidgetitem.h"

Job::Job(QString &jobId, QTreeWidgetItem *parentTreeWidgetItem)
{
   setupUi(this);
   m_closeable = true;
   pgInitialize(tr("Job"), parentTreeWidgetItem);
   QTreeWidgetItem* thisitem = mainWin->getFromHash(this);
   thisitem->setIcon(0,QIcon(QString::fromUtf8(":images/joblog.png")));
   m_cursor = new QTextCursor(textJobLog->document());

   m_jobId = jobId;
   getFont();
   populateText();
   populateForm();
   populateVolumes();
   dockPage();
   setCurrent();
}

void Job::getFont()
{
   QFont font = textJobLog->font();

   QString dirname;
   m_console->getDirResName(dirname);
   QSettings settings(dirname, "bat");
   settings.beginGroup("Console");
   font.setFamily(settings.value("consoleFont", "Courier").value<QString>());
   font.setPointSize(settings.value("consolePointSize", 10).toInt());
   font.setFixedPitch(settings.value("consoleFixedPitch", true).toBool());
   settings.endGroup();
   textJobLog->setFont(font);
}

/*
 * Populate the text in the window
 */
void Job::populateText()
{
   QString query;
   query = "SELECT Time, LogText FROM Log WHERE JobId='" + m_jobId + "' order by Time";

   /* This could be a log item */
   if (mainWin->m_sqlDebug) {
      Pmsg1(000, "Log query cmd : %s\n", query.toUtf8().data());
   }
  
   QStringList results;
   if (m_console->sql_cmd(query, results)) {

      if (!results.size()) {
         QMessageBox::warning(this, tr("Bat"),
            tr("There were no results!\n"
	       "It is possible you may need to add \"catalog = all\" "
	       "to the Messages resource for this job.\n"), QMessageBox::Ok);
	 return;
      } 

      QString jobstr("JobId "); /* FIXME: should this be translated ? */
      jobstr += m_jobId;

      QString htmlbuf("<html><body><pre>");
  
      /* Iterate through the lines of results. */
      QString field;
      QStringList fieldlist;
      QString lastTime;
      QString lastSvc;
      foreach (QString resultline, results) {
         fieldlist = resultline.split("\t");
	 
	 if (fieldlist.size() < 2)
	    continue;

	 QString curTime = fieldlist[0].trimmed();

	 field = fieldlist[1].trimmed();
	 int colon = field.indexOf(":");
	 if (colon > 0) {
 	    /* string is like <service> <jobId xxxx>: ..." 
	     * we split at ':' then remove the jobId xxxx string (always the same) */ 
	    QString curSvc(field.left(colon).replace(jobstr,"").trimmed());
	    if (curSvc == lastSvc  && curTime == lastTime) {
	       curTime.clear();
	       curSvc.clear(); 
	    } else {
	       lastTime = curTime;
	       lastSvc = curSvc;
	    }
//	    htmlbuf += "<td>" + curTime + "</td>";
	    htmlbuf += "\n" + curSvc + " ";

	    /* rest of string is marked as pre-formatted (here trimming should
	     * be avoided, to preserve original formatting) */
	    QString msg(field.mid(colon+2));
	    if (msg.startsWith( tr("Error:")) ) { /* FIXME: should really be translated ? */
 	       /* error msg, use a specific class */
	       htmlbuf += "</pre><pre class=err>" + msg + "</pre><pre>";
	    } else {
	       htmlbuf += msg ;
	    }
	 } else {
 	    /* non standard string, place as-is */
	    if (curTime == lastTime) {
	       curTime.clear();
	    } else {
	       lastTime = curTime;
	    }
//	    htmlbuf += "<td>" + curTime + "</td>";
	    htmlbuf += "\n" + field ;
	 }
  
      } /* foreach resultline */

      htmlbuf += "</pre></body></html>";

      /* full text ready. Here a custom sheet is used to align columns */
      QString logSheet(".err {color:#FF0000;}");
      textJobLog->document()->setDefaultStyleSheet(logSheet);
      textJobLog->document()->setHtml(htmlbuf); 
      textJobLog->moveCursor(QTextCursor::Start);

   } /* if results from query */
  
}

// Need to use the fmtwidgetitem helper instead
QString convertBytesSI(qint64 qfld)
{
   static const qint64 KB = Q_INT64_C(1000);
   static const qint64 MB = (KB * KB);
   static const qint64 GB = (MB * KB);
   static const qint64 TB = (GB * KB);
   static const qint64 PB = (TB * KB);
   static const qint64 EB = (PB * KB);

   /* note: division is integer, so to have some decimals we divide for a
      smaller unit (e.g. GB for a TB number and so on) */
   char suffix;
   if (qfld >= EB) {
      qfld /= PB; 
      suffix = 'E';
   }
   else if (qfld >= PB) {
      qfld /= TB; 
      suffix = 'P';
   }
   else if (qfld >= TB) {
      qfld /= GB; 
      suffix = 'T';
   }
   else if (qfld >= GB) {
      qfld /= MB;
      suffix = 'G';
   }
   else if (qfld >= MB) {
      qfld /= KB;
      suffix = 'M';
   }
   else if (qfld >= KB) {
      suffix = 'k'; /* SI uses lowercase k */
   }
   else  {
      /* plain bytes, no need to reformat */
      return QString("%1 B").arg(qfld); 
   }

   /* having divided for a smaller unit, now we can safely convert to double and
      use the extra room for decimals */
   return QString("%1 %2B").arg(qfld / 1000.0, 0, 'f', 2).arg(suffix);
}

/*
 * Populate the text in the window
 */
void Job::populateForm()
{
   QString stat;
   char buf[256];
   QString query = 
      "SELECT JobId, Job.Name, Level, Client.Name, Pool.Name, FileSet, SchedTime, StartTime, EndTime, "
      "EndTime - StartTime AS Duration, JobBytes, JobFiles, JobErrors, JobStatus, PurgedFiles "
      "FROM Job JOIN Client USING (ClientId) LEFT JOIN Pool USING (PoolId) "
      "LEFT JOIN FileSet USING (FileSetId)"
      "WHERE JobId=" + m_jobId; 
   QStringList results;
   if (m_console->sql_cmd(query, results)) {
      QString resultline;
      QStringList fieldlist;

      foreach (resultline, results) { // should have only one result
         fieldlist = resultline.split("\t");
         QStringListIterator fld(fieldlist);
         label_JobId->setText(fld.next());
         label_Name->setText(fld.next());
         
         label_Level->setText(job_level_to_str(fld.next()[0].toAscii()));

         label_Client->setText(fld.next());
         label_Pool->setText(fld.next());
         label_FileSet->setText(fld.next());
         label_SchedTime->setText(fld.next());
         label_StartTime->setText(fld.next());
         label_EndTime->setText(fld.next());
         label_Duration->setText(fld.next());

         label_JobBytes->setText(convertBytesSI(fld.next().toULongLong()));
         label_JobFiles->setText(fld.next());
         label_JobErrors->setText(fld.next());

         stat=fld.next();
         label_JobStatus->setPixmap(QPixmap(":/images/" + stat + ".png"));
         jobstatus_to_ascii_gui(stat[0].toAscii(), buf, sizeof(buf));
         stat = buf;
         label_JobStatus->setToolTip(stat);

         chkbox_PurgedFiles->setCheckState(fld.next().toInt()?Qt::Checked:Qt::Unchecked);
      }
   }
}
  
void Job::populateVolumes()
{

   QString query = 
      "SELECT DISTINCT VolumeName, InChanger, Slot "
      "FROM Job JOIN JobMedia USING (JobId) JOIN Media USING (MediaId) "
      "WHERE JobId=" + m_jobId + " ORDER BY VolumeName "; 
   Pmsg1(000, "Query cmd : %s\n",query.toUtf8().data());
         

   QStringList results;
   if (m_console->sql_cmd(query, results)) {
      QString resultline;
      QStringList fieldlist;
      list_Volume->clear();
      foreach (resultline, results) { // should have only one result
         fieldlist = resultline.split("\t");
         QStringListIterator fld(fieldlist);
//         QListWidgetItem(QIcon(":/images/inchanger" + fld.next() + ".png"), 
//                         fld.next(), list_Volume);
         list_Volume->addItem(fld.next());
      }
   }
}

//QListWidgetItem ( const QIcon & icon, const QString & text, QListWidget * parent = 0, int type = Type )