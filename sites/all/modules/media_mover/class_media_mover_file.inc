<?php

// $Id$

/**
 * @file
 * Base class for media mover files
 *
 * @TODO $this->status should probably be protected and set only with status_set()
 */


class media_mover_file {



  /**
   * By default load the file
   */
  function __construct($input = NULL) {
    if (empty($input)) {
      $this->status = MMA_FILE_STATUS_READY;
      $this->new = TRUE;
      $this->step_order = 0;
    }

    // If the input is a mmfid try to load the file
    else if (is_string($input) || is_int($input)) {
      $this->load($input);
    }

    // If the input is a media mover step we can construct the stub file from
    // a default set of data
    else if (is_object($input) && get_class($input) == 'media_mover_step') {
      $defaults['cid'] = $input->cid;
      $defaults['step_order'] = $input->step_order;
      // Add the defaults to the new file
      foreach ($defaults as $key => $value) {
        $this->{$key} = $value;
      }
    }
  }


  /**
   * Get media mover file data for the requested id.
   *
   * @param $mmfid
   *   Media Mover file id
   * @return
   *   Boolean, did the file load or not?
   *
   */
  function load($mmfid) {
    // Load the file
    // @TODO how much integration should there be between managed drupal files?

    $query = db_select('media_mover_files', 'mmf')
      ->condition('mmf.mmfid', $mmfid, '=')
      ->fields('mmf');
    $result = $query->execute()->fetchAssoc();

    // No file data was found
    if (! $result) {
      return FALSE;
    }

    // Load the data onto the file
    $this->load_data($result);

    // Allow the file to be altered
    drupal_alter('media_mover_file_load', $this);
  }


  /**
   * Save data associated with a file.
   *
   * @NOTE this function is not thread safe. $file->lock/unlock() should be used
   *       to prevent this file from being hijacked by a new thread.
   *
   * @TODO this should not save $file->status, only $file->unlock() should do this
   *
   * @param boolean $advance
   *   should the file's current step be advanced?
   */
  function save($advance = FALSE) {
    // Set a default status
    if (empty($this->status)) {
      $this->status = MMA_FILE_STATUS_READY;
    }

    if (empty($this->date)) {
      $this->date = time();
    }
    if (empty($this->source_uri)) {
      $this->source_uri = $this->uri;
    }
    // If a filesize was not passed in see if we can get one.
    if ((! $filesize = $this->data_get('filesize')) && file_exists($this->uri)) {
      $this->data_set('filesize', filesize($this->uri));
    }

    // Advance the step for this file if requested
    if ($advance) {
      $this->step_next();
    }

    // Alter the file
    drupal_alter('media_mover_file_presave', $this);

    // If this is a programatic usage do not access the database
    // @TODO this needs rethinking
    if (! empty($this->passthrough)) {
      return;
    }

    // Save it
    $status = drupal_write_record('media_mover_files', $this, empty($this->mmfid) ? array() : array('mmfid'));
    if (! $status) {
      return FALSE;
    }

    // Flag the file as saved
    $this->new = FALSE;

    return $status;
  }


  /**
   * Check to see if this file is currently locked
   *
   * @NOTE this will return TRUE even if the file is already locked. This is a
   *       potential hazzard if a file that is locked is already being passed
   *       into the system.
   *
   * @return boolean
   */
  function locked() {
    if (! empty($this->status) && $this->status == MMA_FILE_STATUS_LOCKED) {
      return TRUE;
    }
    return FALSE;
  }

  /**
   * Set the status to locked
   *
   * Locks the file status to prevent it from being used by multiple processes.
   * This will lock any file's status to locked. Functions implementing custom
   * statuses need to be careful.
   *
   * @TODO can an already locked file return TRUE here? This is mostly to fix and
   *       issue with
   *
   * @return boolean
   */
  function lock() {
    if (! $this->locked()) {
      $this->lock_date = time();
      // Was the status correctly set?
      return $this->status_set('*');
    }
    return TRUE;
  }


  /**
   * Unlock a file for further use. Only saves status and unlock time.
   *
   * This is used as the last step in a thread safe handling of a file. If a file
   * is locked this will unlock the file and save the status. If this file is
   * not locked it will simply save the current status of the file. This is used
   * by steps that implement custom file statuses.
   *
   * @param $status
   *   String, status to set this file to
   */
  function unlock($status = MMA_FILE_STATUS_READY) {
    // If this file is still locked from processing it can be unlocked. Files
    // with errors or custom status do not need to be updated.
    if ($this->locked()) {
      $this->lock_date = 0;
      // If this is file is on the last step in the configuration it is finished.
      $configuration = media_mover_api_configuration_load($this->cid);
      // Is this the last step for this file.
      // @NOTE $configuration is not required to unlock a file
      if (! empty($this->cid) && $configuration->last_step($this->step_order)) {
        $this->status = MMA_FILE_STATUS_FINISHED;
      }
      else {
        $this->status = $status;
      }
    }

    $this->status_set('*', $this->status);

    return $this->status;
  }


  /**
   * Updates a file's URI
   *
   * As the file moves from step to step. Note that this is not thread safe.
   *
   * @param $filepath
   *   string, filepath
   * @param $step_oder
   *   int, what step
   */
  function update_uri($uri, $step) {
    // Some modules may return the filepath as TRUE or return FALSE
    // if there was an error. In either case we do nothing.
    if (empty($uri) || $uri === TRUE) {
      $uri = $this->uri;
    }

    $this->data_set('steps', array(
      'step_order' => $step->step_order,
      'sid' => $step->sid,
      'result_uri' => $uri,
    ));

    // Update the current uri
    $this->uri = $uri;
  }


  /**
   * API to permantly store file data. Will overwrite existing data. Brital. Sorry.
   *
   * @param string $name
   *   Name of the data to store.
   * @param array, string $value
   *   If $value is an array, the array should be formated as
   *   array('id' => ID, 'value' => VALUE).
   */
  function data_set($key, $value) {
    if (is_string($value) || is_int($value)) {
      $this->data[$key] = $value;
    }
    // Non string data
    else {
      // A plain array is being stored at no specified ID
      if (! isset($value['id'])) {
        $this->data[$key][] = $value;
      }
      else {
        $this->data[$key][$value['id']] = $value['value'];
      }
    }

  }


  /**
   * API function to retrieve data from the file
   */
  function data_get($key) {
    if (! empty($this->data[$key])) {
      return $this->data[$key];
    }
    return FALSE;
  }


  /**
   * API function to remove data from the file
   *
   * @param string $key
   * @param string $index
   */
  function data_delete($key, $index = NULL) {
    if ($index) {
      unset($this->data[$key][$index]);
    }
    else {
      unset($this->data[$key]);
    }
  }

  /**
   * Return a node or nid from a file if it exists
   *
   * @return object
   */
  function node_get($load = TRUE) {
    if (! $nid = $this->nid) {
      if (! $nid = $this->data['node']->nid) {
        return FALSE;
      }
    }
    if ($load) {
      return node_load($nid);
    }
    return $nid;
  }


  /**
   * Utility function: return file steps filtered by module
   *
   * @param $module_name
   *   String, module name
   * @return array of steps that match $module_name
   */
  function steps_filtered_by_module($module_name) {
    $steps = array();
    if ($this->steps) {
      foreach ($this->steps as $step) {
        if ($step->build['module'] == $module_name) {
          $steps[$step->step_order] = $step;
        }
      }
      if ($steps) {
        return $steps;
      }
    }
    return FALSE;
  }


  /**
   * Advance the file step
   *
   * Moves the file one step forward and sets the file status.
   */
  private function step_next() {
    // Load the configuration
    $configuration = media_mover_api_configuration_load($this->cid);
    // Advance the file step if this is not the last step in the configuration.
    if (! $configuration->last_step($this->step_order) ) {
      $this->step_order = $this->step_order + 1;
    }
    // This is the final step.
    else  {
      $this->status = MMA_FILE_STATUS_FINISHED;
      $this->lock_date = 0;
    }
  }


  /**
   * Helper function to get a uri from a specified step
   *
   * @TODO this is likely broken right now
   *
   * @param $data
   *   string or array, is either a $sid or an array(MODULE_NAME, ACTION)
   * @return string, filepath
   */
  function retrive_uri($data) {
    if (is_array($data)) {
      foreach ($this->steps as $step) {
        if ($step->module == $data['module'] && $step->action_id == $data['action_id']) {
          $sid = $step->sid;
        }
      }
    }
    if (is_string($data)) {
      $sid = $data;
    }
    return $this->data['files'][$sid];
  }


  /**
   * Delete a single Media Mover file.
   *
   * NOTE DO NOT delete source file unless explicitly told to because it could
   * delete files that are in use by Drupal elsewhere
   *
   * @TODO files need to figure out if they are being processed and only be
   *       deleted *after* they are finished being processed
   */
  function delete($source_delete = FALSE) {
    if (! empty($this->steps)) {
      // By default use steps > 0
      if (! $source_delete) {
        $steps = array_slice($this->steps, 1);
      }
      else {
        $steps = $this->steps;
      }
      foreach ($steps as $id => $step) {
        // Is there a specific callback for this delete function?
        if ($function = $step->build['delete']) {
          if (function_exists($function)) {
            $function($file, $step);
          }
        }
        else if (file_exists($step->uri)) {
          file_delete($step->uri);
        }
      }
    }

    // Remove the file from the database
    db_query("DELETE FROM {media_mover_files} WHERE mmfid = :mmfid", array(':mmfid' => $this->mmfid));
  }


  /**
   * Returns the filepath that should be reprocessed
   *
   * @param unknown_type $step
   */
  function reprocess_filepath($step = 0) {
    // the first step should return original file
    if ($step === 0) {
      return $this->source_uri;
    }
    return $this->steps[$step]->uri;
  }


  /**
   * Utility function to run the next step on this file.
   */
  function process_next() {
    // We need the configuration that created this file
    $configuration = media_mover_api_configuration_load($this->cid);
    // Step to execute is one ahead of current file step
    //$step_order = $this->step_order++;
    // $step_order = $this->step_order;
    // Run the step
    return $configuration->steps[$this->step_order]->run($this);
  }


  /**
   * Set file status
   *
   * This function is intended to be used only for changing a file's status.
   * $file->save() will not change the file's status
   *
   * @param $status_check
   *   String, the status state to check if this file is in, use * for any status
   * @param $status_change
   *   String, the status to set the file to
   * @param $time
   *   Int, unix time stamp
   * @return boolean could the file status be set?
   */
  function status_set($status_check = MMA_FILE_STATUS_READY, $status_change = MMA_FILE_STATUS_LOCKED, $time = FALSE) {
    // Only lock if the file is in the db, note that this
    // prevents locking if we are passing through a file
    // rather than saving it to the db
    if (empty($this->mmfid) || ! empty($this->pass_through)) {
      return TRUE;
    }

    // Open a transaction to keep this thread safe.
    $transaction = db_transaction();

    try {
      $query = db_query("SELECT status FROM {media_mover_files} WHERE mmfid = :mmfid", array(
        ':mmfid' => $this->mmfid
      ));
      $result = $query->fetchObject();

      if ($result->status == $status_check || $status_check == '*' ) {

        // Update the status in the DB
        $this->status = $status_change;
        // Create a dummy object so only lock status is saved- this is probably
        // never needed, but this function explicitly only sets the status of
        // the file so an implementor may unexpected results if we save the full
        // object
        $dummy = new stdClass();
        $dummy->mmfid = $this->mmfid;
        $dummy->status = $this->status;
        $dummy->lock_date = $this->lock_date;
        return drupal_write_record('media_mover_files', $dummy, array('mmfid'));
      }
    }
    catch (Exception $error) {
      $transaction->rollback();
      return FALSE;
    }
  }


  /**
   * Utility function to add data to the file
   *
   * @param $data
   *   Object, data to add to the file
   */
  private function load_data($data) {
    // Make sure we do not have serialized data
    if (! is_array($data['data'])) {
      $data['data'] = unserialize($data['data']);
    }
    // Make sure that we have a valid status @TODO is this right?
    if ($data['status'] == NULL) {
      $data['status'] = MMA_FILE_STATUS_READY;
    }
    // Add the data back onto the file
    foreach ($data as $key => $value) {
      $this->{$key} = $value;
    }
  }


  /**
   * Utility function to queue a file.
   */
  function queue() {
    // Set the status to queued
    return $this->status_set(MMA_FILE_STATUS_READY, MMA_FILE_STATUS_QUEUED);
  }


  /**
   * Utility function to remove a file from the queued state
   */
  function dequeue($status = MMA_FILE_STATUS_LOCKED) {
    // Set the status to queued
    return $this->status_set(MMA_FILE_STATUS_QUEUED, $status);
  }


}
