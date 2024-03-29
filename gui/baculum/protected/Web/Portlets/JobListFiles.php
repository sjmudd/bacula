<?php
/*
 * Bacula(R) - The Network Backup Solution
 * Baculum   - Bacula web interface
 *
 * Copyright (C) 2013-2019 Kern Sibbald
 *
 * The main author of Baculum is Marcin Haba.
 * The original author of Bacula is Kern Sibbald, with contributions
 * from many others, a complete list can be found in the file AUTHORS.
 *
 * You may use this file and others of this release according to the
 * license defined in the LICENSE file, which includes the Affero General
 * Public License, v3.0 ("AGPLv3") and some additional permissions and
 * terms pursuant to its AGPLv3 Section 7.
 *
 * This notice must be preserved when any source code is
 * conveyed and/or propagated.
 *
 * Bacula(R) is a registered trademark of Kern Sibbald.
 */

Prado::using('System.Web.UI.ActiveControls.TActiveLabel');
Prado::using('System.Web.UI.ActiveControls.TActiveTextBox');
Prado::using('System.Web.UI.ActiveControls.TActiveDropDownList');
Prado::using('System.Web.UI.ActiveControls.TActiveRepeater');
Prado::using('System.Web.UI.ActiveControls.TCallback');
Prado::using('System.Web.UI.WebControls.TListItem');
Prado::using('Application.Web.Portlets.Portlets');

/**
 * Job list files control.
 *
 * @author Marcin Haba <marcin.haba@bacula.pl>
 * @category Control
 * @package Baculum Web
 */
class JobListFiles extends Portlets {

	const JOBID = 'JobId';

	const DEFAULT_PAGE_SIZE = 100;

	public function onLoad($param) {
		parent::onLoad($param);
		if ($this->getPage()->IsPostBack || $this->getPage()->IsCallBack) {
			return;
		}
		$this->FileListOffset->Text = 0;
		$this->FileListLimit->Text = self::DEFAULT_PAGE_SIZE;
	}

	public function loadFileList($sender, $param) {
		$params = array(
			'offset' => intval($this->FileListOffset->Text),
			'limit' => intval($this->FileListLimit->Text)
		);
		if (!empty($this->FileListType->SelectedValue)) {
			$params['type'] = $this->FileListType->SelectedValue;
		}
		if (!empty($this->FileListSearch->Text)) {
			$params['search'] = $this->FileListSearch->Text;
		}
		$query = '?' . http_build_query($params);
		$result = $this->getModule('api')->get(
			array('jobs', $this->getJobId(), 'files', $query)
		);
		if ($result->error === 0) {
			$file_list = $result->output->items;
			if (!empty($this->FileListSearch->Text)) {
				$file_list = $this->findFileListItems($file_list, $this->FileListSearch->Text);
			}
			$this->FileList->DataSource = $file_list;
			$this->FileList->dataBind();
			$this->FileListCount->Text = $result->output->total;
		} else {
			$this->FileList->DataSource = array();
			$this->FileList->dataBind();
			$this->FileListCount->Text = 0;
		}
	}

	private function findFileListItems($file_list, $keyword) {
		$result = array();
		for ($i = 0; $i < count($file_list); $i++) {
			$pos = stripos($file_list[$i], $keyword);
			$str1 = substr($file_list[$i], 0, $pos);
			$key_len = strlen($keyword);
			$key = substr($file_list[$i], $pos, $key_len);
			$str2 = substr($file_list[$i], ($pos + $key_len));
			$result[] = $str1 . '<strong class="w3-text-red">' . $key . '</strong>' . $str2;
		}
		return $result;
	}

	/**
	 * Set job identifier to show files.
	 *
	 * @return none
	 */
	public function setJobId($jobid) {
		$jobid = intval($jobid);
		$this->setViewState(self::JOBID, $jobid, 0);
	}

	/**
	 * Get job identifier to show files.
	 *
	 * @return integer job identifier
	 */
	public function getJobId() {
		return $this->getViewState(self::JOBID, 0);
	}
}
?>
