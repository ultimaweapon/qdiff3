/***************************************************************************
 *   Copyright (C) 2003-2011 by Joachim Eibl                               *
 *   joachim.eibl at gmx.de                                                *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "progress.h"
#include "common.h"

#include <QProgressBar>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QApplication>
#include <QThread>
#include <QStatusBar>
#include <kio/job.h>

#include <klocale.h>

ProgressDialog* g_pProgressDialog=0;

ProgressDialog::ProgressDialog( QWidget* pParent, QStatusBar* pStatusBar )
: QDialog(pParent), m_pStatusBar(pStatusBar)
{
   m_pGuiThread = QThread::currentThread();

   setObjectName("ProgressDialog");
   m_bStayHidden = false;
   setModal(true);
   QVBoxLayout* layout = new QVBoxLayout(this);

   m_pInformation = new QLabel( " ", this );
   layout->addWidget( m_pInformation );

   m_pProgressBar = new QProgressBar();
   m_pProgressBar->setRange(0,1000);
   layout->addWidget( m_pProgressBar );

   m_pSubInformation = new QLabel( " ", this);
   layout->addWidget( m_pSubInformation );

   m_pSubProgressBar = new QProgressBar();
   m_pSubProgressBar->setRange(0,1000);
   layout->addWidget( m_pSubProgressBar );

   m_pSlowJobInfo = new QLabel( " ", this);
   layout->addWidget( m_pSlowJobInfo );

   QHBoxLayout* hlayout = new QHBoxLayout();
   layout->addLayout(hlayout);
   hlayout->addStretch(1);
   m_pAbortButton = new QPushButton( i18n("&Cancel"), this);
   hlayout->addWidget( m_pAbortButton );
   connect( m_pAbortButton, SIGNAL(clicked()), this, SLOT(slotAbort()) );

   if (m_pStatusBar)
   {
      m_pStatusBarWidget = new QWidget;
      QHBoxLayout* pStatusBarLayout = new QHBoxLayout(m_pStatusBarWidget);
      pStatusBarLayout->setMargin(0);
      pStatusBarLayout->setSpacing(3);
      m_pStatusProgressBar = new QProgressBar;
      m_pStatusProgressBar->setRange(0, 1000);
      m_pStatusProgressBar->setTextVisible(false);
      m_pStatusAbortButton = new QPushButton( i18n("&Cancel") );
      connect(m_pStatusAbortButton, SIGNAL(clicked()), this, SLOT(slotAbort()));
      pStatusBarLayout->addWidget(m_pStatusProgressBar);
      pStatusBarLayout->addWidget(m_pStatusAbortButton);
      m_pStatusBar->addPermanentWidget(m_pStatusBarWidget,0);
      m_pStatusBarWidget->setFixedHeight(m_pStatusBar->height());
      m_pStatusBarWidget->hide();
   }
   else
   {
      m_pStatusProgressBar = 0;
      m_pStatusAbortButton = 0;
   }

   m_progressDelayTimer = 0;
   m_delayedHideTimer = 0;
   m_delayedHideStatusBarWidgetTimer = 0;
   resize(400, 100);
   m_t1.start();
   m_t2.start();
   m_bWasCancelled = false;
   m_eCancelReason = eUserAbort;
   m_pJob = 0;
}

void ProgressDialog::setStayHidden( bool bStayHidden )
{
   if (m_bStayHidden != bStayHidden)
   {
      m_bStayHidden = bStayHidden;
      if (m_pStatusBarWidget)
      {
         if (m_bStayHidden)
         {
            if (m_delayedHideStatusBarWidgetTimer)
            {
               killTimer(m_delayedHideStatusBarWidgetTimer);
               m_delayedHideStatusBarWidgetTimer = 0;
            }
            m_pStatusBarWidget->show();
         }
         else
            hideStatusBarWidget();  // delayed
      }
      if ( isVisible() && m_bStayHidden )
         hide();  // delayed hide
   }
}

void ProgressDialog::push()
{
   ProgressLevelData pld;
   if ( !m_progressStack.empty() )
   {
      pld.m_dRangeMax = m_progressStack.back().m_dSubRangeMax;
      pld.m_dRangeMin = m_progressStack.back().m_dSubRangeMin;
   }
   else
   {
      m_bWasCancelled = false;
      m_t1.restart();
      m_t2.restart();
      if ( !m_bStayHidden )
         show();
   }

   m_progressStack.push_back( pld );
}

void ProgressDialog::pop( bool bRedrawUpdate )
{
   if ( !m_progressStack.empty() )
   {
      m_progressStack.pop_back();
      if (m_progressStack.empty())
      {
         hide();
      }
      else
         recalc(bRedrawUpdate);
   }
}

void ProgressDialog::setInformation(const QString& info, int current, bool bRedrawUpdate )
{
   if ( m_progressStack.empty() )
      return;
   ProgressLevelData& pld = m_progressStack.back();
   pld.m_current = current;
   int level = m_progressStack.size();
   if ( level==1 )
   {
      m_pInformation->setText( info );
      m_pSubInformation->setText("");
      if (m_pStatusBar && m_bStayHidden)
         m_pStatusBar->showMessage(info);
   }
   else if ( level==2 )
   {
      m_pSubInformation->setText( info );
   }
   recalc(bRedrawUpdate);
}

void ProgressDialog::setInformation(const QString& info, bool bRedrawUpdate )
{
   if ( m_progressStack.empty() )
      return;
   //ProgressLevelData& pld = m_progressStack.back();
   int level = m_progressStack.size();
   if ( level==1 )
   {
      m_pInformation->setText( info );
      m_pSubInformation->setText( "" );
      if (m_pStatusBar && m_bStayHidden)
         m_pStatusBar->showMessage(info);
   }
   else if ( level==2 )
   {
      m_pSubInformation->setText( info );
   }
   recalc(bRedrawUpdate);
}

void ProgressDialog::setMaxNofSteps( int maxNofSteps )
{
   if ( m_progressStack.empty() )
      return;
   ProgressLevelData& pld = m_progressStack.back();
   pld.m_maxNofSteps = maxNofSteps;
   pld.m_current = 0;
}

void ProgressDialog::addNofSteps( int nofSteps )
{
   if ( m_progressStack.empty() )
      return;
   ProgressLevelData& pld = m_progressStack.back();
   pld.m_maxNofSteps.fetchAndAddRelaxed( nofSteps );
}

void ProgressDialog::step( bool bRedrawUpdate )
{
   if ( m_progressStack.empty() )
      return;
   ProgressLevelData& pld = m_progressStack.back();
   pld.m_current.fetchAndAddRelaxed(1);
   recalc(bRedrawUpdate);
}

void ProgressDialog::setCurrent( int subCurrent, bool bRedrawUpdate )
{
   if ( m_progressStack.empty() )
      return;
   ProgressLevelData& pld = m_progressStack.back();
   pld.m_current = subCurrent;
   recalc( bRedrawUpdate );
}

// The progressbar goes from 0 to 1 usually.
// By supplying a subrange transformation the subCurrent-values
// 0 to 1 will be transformed to dMin to dMax instead.
// Requirement: 0 < dMin < dMax < 1
void ProgressDialog::setRangeTransformation( double dMin, double dMax )
{
   if ( m_progressStack.empty() )
      return;
   ProgressLevelData& pld = m_progressStack.back();
   pld.m_dRangeMin = dMin;
   pld.m_dRangeMax = dMax;
   pld.m_current = 0;
}

void ProgressDialog::setSubRangeTransformation( double dMin, double dMax )
{
   if ( m_progressStack.empty() )
      return;
   ProgressLevelData& pld = m_progressStack.back();
   pld.m_dSubRangeMin = dMin;
   pld.m_dSubRangeMax = dMax;
}

void qt_enter_modal(QWidget*);
void qt_leave_modal(QWidget*);

void ProgressDialog::enterEventLoop( KJob* pJob, const QString& jobInfo )
{
   m_pJob = pJob;
   m_currentJobInfo = jobInfo;
   m_pSlowJobInfo->setText( m_currentJobInfo );
   if ( m_progressDelayTimer )
      killTimer( m_progressDelayTimer );
   m_progressDelayTimer = startTimer( 3000 ); /* 3 s delay */

   // immediately show the progess dialog for KIO jobs, because some KIO jobs require password authentication,
   // but if the progress dialog pops up at a later moment, this might cover the login dialog and hide it from the user.
   if( m_pJob && !m_bStayHidden )
      show();

   // instead of using exec() the eventloop is entered and exited often without hiding/showing the window.
   //qt_enter_modal(this);
   QEventLoop* pEventLoop = new QEventLoop(this);
   m_eventLoopStack.push_back( pEventLoop );
   pEventLoop->exec(); // this function only returns after ProgressDialog::exitEventLoop() is called.
   delete pEventLoop;
   m_eventLoopStack.pop_back();
   //qt_leave_modal(this);
}

void ProgressDialog::exitEventLoop()
{
   if ( m_progressDelayTimer )
      killTimer( m_progressDelayTimer );
   m_progressDelayTimer = 0;
   m_pJob = 0;
   if (!m_eventLoopStack.empty())
      m_eventLoopStack.back()->exit();
}

void ProgressDialog::recalc(bool bUpdate)
{
   if (!m_bWasCancelled)
   {
      if (QThread::currentThread() == m_pGuiThread)
      {
         if (m_progressDelayTimer)
            killTimer(m_progressDelayTimer);
         m_progressDelayTimer = 0;
         if ( ! m_bStayHidden )
            m_progressDelayTimer = startTimer(3000); /* 3 s delay */

         int level = m_progressStack.size();
         if ((bUpdate && level == 1) || m_t1.elapsed() > 200)
         {
            if (m_progressStack.empty())
            {
               m_pProgressBar->setValue(0);
               m_pSubProgressBar->setValue(0);
            }
            else
            {
               QList<ProgressLevelData>::iterator i = m_progressStack.begin();
               int value = int(1000.0 * (getAtomic(i->m_current) * (i->m_dRangeMax - i->m_dRangeMin) / getAtomic(i->m_maxNofSteps) + i->m_dRangeMin));
               m_pProgressBar->setValue(value);
               if (m_bStayHidden && m_pStatusProgressBar)
                  m_pStatusProgressBar->setValue(value);

               ++i;
               if (i != m_progressStack.end())
                  m_pSubProgressBar->setValue(int(1000.0 * (getAtomic(i->m_current) * (i->m_dRangeMax - i->m_dRangeMin) / getAtomic(i->m_maxNofSteps) + i->m_dRangeMin)));
               else
                  m_pSubProgressBar->setValue(int(1000.0 * m_progressStack.front().m_dSubRangeMin));
            }

            if (!m_bStayHidden && !isVisible())
               show();
            qApp->processEvents();
            m_t1.restart();
         }
      }
      else
      {
         QMetaObject::invokeMethod(this, "recalc", Qt::QueuedConnection, Q_ARG(bool, bUpdate));
      }
   }
}


#include <QTimer>
void ProgressDialog::show()
{
   if ( m_progressDelayTimer )
      killTimer( m_progressDelayTimer );
   if ( m_delayedHideTimer )
      killTimer( m_delayedHideTimer );
   m_progressDelayTimer = 0;
   m_delayedHideTimer = 0;
   if ( !isVisible() && (parentWidget()==0 || parentWidget()->isVisible()) )
   {
      QDialog::show();
   }
}

void ProgressDialog::hide()
{
   if ( m_progressDelayTimer )
      killTimer( m_progressDelayTimer );
   m_progressDelayTimer = 0;
   // Calling QDialog::hide() directly doesn't always work. (?)
   if (m_delayedHideTimer)
      killTimer(m_delayedHideTimer);
   m_delayedHideTimer = startTimer(100);
}

void ProgressDialog::delayedHide()
{
   if (m_pJob!=0)
   {
      m_pJob->kill( KJob::Quietly );
      m_pJob = 0;
   }
   QDialog::hide();
   m_pInformation->setText( "" );

   //m_progressStack.clear();

   m_pProgressBar->setValue( 0 );
   m_pSubProgressBar->setValue( 0 );
   m_pSubInformation->setText("");
   m_pSlowJobInfo->setText("");
}

void ProgressDialog::hideStatusBarWidget()
{
   if (m_delayedHideStatusBarWidgetTimer)
      killTimer(m_delayedHideStatusBarWidgetTimer);
   m_delayedHideStatusBarWidgetTimer = startTimer(100);
}

void ProgressDialog::delayedHideStatusBarWidget()
{
   if (m_progressDelayTimer)
      killTimer(m_progressDelayTimer);
   m_progressDelayTimer = 0;
   if (m_pStatusBarWidget)
   {
      m_pStatusBarWidget->hide();
      m_pStatusProgressBar->setValue(0);
      m_pStatusBar->clearMessage();
   }
}


void ProgressDialog::reject()
{
   cancel(eUserAbort);
   QDialog::reject();
}

void ProgressDialog::slotAbort()
{
   reject();
}

bool ProgressDialog::wasCancelled()
{
   if ( QThread::currentThread() == m_pGuiThread )
   {
      if( m_t2.elapsed()>100 )
      {
         qApp->processEvents();
         m_t2.restart();
      }
   }
   return m_bWasCancelled;
}

void ProgressDialog::clearCancelState()
{
   m_bWasCancelled = false;
}

void ProgressDialog::cancel(e_CancelReason eCancelReason)
{
   if ( !m_bWasCancelled)
   {
      m_bWasCancelled = true;
      m_eCancelReason = eCancelReason;
   }
}

ProgressDialog::e_CancelReason ProgressDialog::cancelReason()
{
   return m_eCancelReason;
}

void ProgressDialog::timerEvent(QTimerEvent* te )
{
   if ( te->timerId() == m_progressDelayTimer )
   {
      if( !isVisible() && !m_bStayHidden )
      {
         show();
      }
      m_pSlowJobInfo->setText( m_currentJobInfo );
   }
   else if (te->timerId() == m_delayedHideTimer)
   {
      killTimer(m_delayedHideTimer);
      m_delayedHideTimer = 0;
      delayedHide();
   }
   else if (te->timerId() == m_delayedHideStatusBarWidgetTimer)
   {
      killTimer(m_delayedHideStatusBarWidgetTimer);
      m_delayedHideStatusBarWidgetTimer = 0;
      delayedHideStatusBarWidget();
   }
}


ProgressProxy::ProgressProxy()
{
   g_pProgressDialog->push();
}

ProgressProxy::~ProgressProxy()
{
   g_pProgressDialog->pop(false);
}

void ProgressProxy::enterEventLoop( KJob* pJob, const QString& jobInfo )
{
  g_pProgressDialog->enterEventLoop(pJob, jobInfo);
}

void ProgressProxy::exitEventLoop()
{
  g_pProgressDialog->exitEventLoop();
}

QDialog *ProgressProxy::getDialog()
{
  return g_pProgressDialog;
}

void ProgressProxy::setInformation( const QString& info, bool bRedrawUpdate )
{
   g_pProgressDialog->setInformation( info, bRedrawUpdate );
}

void ProgressProxy::setInformation( const QString& info, int current, bool bRedrawUpdate )
{
   g_pProgressDialog->setInformation( info, current, bRedrawUpdate );
}

void ProgressProxy::setCurrent( int current, bool bRedrawUpdate  )
{
   g_pProgressDialog->setCurrent( current, bRedrawUpdate );
}

void ProgressProxy::step( bool bRedrawUpdate )
{
   g_pProgressDialog->step( bRedrawUpdate );
}

void ProgressProxy::setMaxNofSteps( int maxNofSteps )
{
   g_pProgressDialog->setMaxNofSteps( maxNofSteps );
}

void ProgressProxy::addNofSteps( int nofSteps )
{
   g_pProgressDialog->addNofSteps( nofSteps );
}

bool ProgressProxy::wasCancelled()
{
   return g_pProgressDialog->wasCancelled();
}

void ProgressProxy::setRangeTransformation( double dMin, double dMax )
{
   g_pProgressDialog->setRangeTransformation( dMin, dMax );
}

void ProgressProxy::setSubRangeTransformation( double dMin, double dMax )
{
   g_pProgressDialog->setSubRangeTransformation( dMin, dMax );
}

void ProgressProxy::recalc()
{
   g_pProgressDialog->recalc(true);
}

